/* -*- Mode: C; c-basic-offset: 4 -*-
 * pygtk- Python bindings for the GTK toolkit.
 * Copyright (C) 1998-2003  James Henstridge
 * Copyright (C) 2004       Johan Dahlin
 *
 *   pygmainloop.c: GMainLoop wrapper
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif
#include <fcntl.h>

#include <Python.h>
#include <pythread.h>
#include <glib.h>

#include "pygmainloop.h"
#include "pygmaincontext.h"
#include "pyglib.h"
#include "pyglib-private.h"

static int pipe_fds[2];

typedef struct {
    GSource source;
    GPollFD fd;
} PySignalWatchSource;

#ifdef DISABLE_THREADING
static GMainLoop *pyg_current_main_loop = NULL;;

static inline GMainLoop *
pyg_save_current_main_loop (GMainLoop *main_loop)
{
    GMainLoop *retval = pyg_current_main_loop;

    g_return_val_if_fail(main_loop != NULL, NULL);

    pyg_current_main_loop = g_main_loop_ref(main_loop);

    return retval;
}

static inline void
pyg_restore_current_main_loop (GMainLoop *main_loop)
{
    if (pyg_current_main_loop != NULL)
	g_main_loop_unref(pyg_current_main_loop);
    pyg_current_main_loop = main_loop;
}

static inline GMainLoop *
pyg_get_current_main_loop (void)
{
    return pyg_current_main_loop;
}
#else /* !defined(#ifndef DISABLE_THREADING) */

#if PY_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 7
static Py_tss_t pyg_current_main_loop_key = Py_tss_NEEDS_INIT;
#else /* pyversion < 3.7 */
static int pyg_current_main_loop_key = -1;
#endif

static inline GMainLoop *
pyg_save_current_main_loop (GMainLoop *main_loop)
{
    GMainLoop *retval;

    g_return_val_if_fail(main_loop != NULL, NULL);

#if PY_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 7
    PyThread_tss_create(&pyg_current_main_loop_key);
    retval = PyThread_tss_get(&pyg_current_main_loop_key);
    PyThread_tss_set(&pyg_current_main_loop_key, g_main_loop_ref(main_loop));
#else
    if (pyg_current_main_loop_key == -1)
	pyg_current_main_loop_key = PyThread_create_key();

    retval = PyThread_get_key_value(pyg_current_main_loop_key);
    PyThread_delete_key_value(pyg_current_main_loop_key);
    PyThread_set_key_value(pyg_current_main_loop_key, 
			   g_main_loop_ref(main_loop));
#endif

    return retval;
}

static inline void
pyg_restore_current_main_loop (GMainLoop *main_loop)
{
    GMainLoop *prev;

#if PY_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 7
    g_return_if_fail (PyThread_tss_is_created(&pyg_current_main_loop_key));
    prev = PyThread_tss_get(&pyg_current_main_loop_key);
    if (prev != NULL)
        g_main_loop_unref(prev);
    if (main_loop != NULL)
        PyThread_tss_set(&pyg_current_main_loop_key, main_loop);
    else
        PyThread_tss_set(&pyg_current_main_loop_key, NULL);
#else /* pyversion < 3.7 */
    g_return_if_fail (pyg_current_main_loop_key != -1);

    prev = PyThread_get_key_value(pyg_current_main_loop_key);
    if (prev != NULL)
	g_main_loop_unref(prev);
    PyThread_delete_key_value(pyg_current_main_loop_key);
    if (main_loop != NULL)
	PyThread_set_key_value(pyg_current_main_loop_key, main_loop);
#endif
}

static inline GMainLoop *
pyg_get_current_main_loop (void)
{
#if PY_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 7
    if (!PyThread_tss_is_created(&pyg_current_main_loop_key))
        return NULL;
    return PyThread_tss_get(&pyg_current_main_loop_key);
#else /* pyversion < 3.7 */
    if (pyg_current_main_loop_key == -1)
	return NULL;
    return PyThread_get_key_value(pyg_current_main_loop_key);
#endif
}
#endif /* DISABLE_THREADING */

static gboolean
pyg_signal_watch_prepare(GSource *source,
			 int     *timeout)
{
    /* Python only invokes signal handlers from the main thread,
     * so if a thread other than the main thread receives the signal
     * from the kernel, PyErr_CheckSignals() from that thread will
     * do nothing.
     */

#ifdef HAVE_PYSIGNAL_SETWAKEUPFD
    return FALSE;
#else /* !HAVE_PYSIGNAL_SETWAKEUPFD */
    /* On Windows g_poll() won't be interrupted by a signal
     * (AFAIK), so we need the timeout there too, even if there's
     * only one thread.
     */
#ifndef PLATFORM_WIN32
    if (!pyglib_threads_enabled())
	return FALSE;
#endif /* PLATFORM_WIN32 */

    /* If we're using 2.5 or an earlier version of python we
     * will default to a timeout every second, be aware,
     * this will cause unnecessary wakeups, see
     * http://bugzilla.gnome.org/show_bug.cgi?id=481569
     */
    *timeout = 1000;
    return FALSE;
#endif /* HAVE_PYSIGNAL_SETWAKEUPFD */
}

static gboolean
pyg_signal_watch_check(GSource *source)
{
    PyGILState_STATE state;
    GMainLoop *main_loop;

#ifdef HAVE_PYSIGNAL_SETWAKEUPFD
    PySignalWatchSource *real_source = (PySignalWatchSource *)source;
    GPollFD *poll_fd = &real_source->fd;
    unsigned char dummy;
    gssize ret;
    if (poll_fd->revents & G_IO_IN)
	ret = read(poll_fd->fd, &dummy, 1);
#endif

    state = pyglib_gil_state_ensure();

    main_loop = pyg_get_current_main_loop();

    if (PyErr_CheckSignals() == -1 && main_loop != NULL) {
	PyErr_SetNone(PyExc_KeyboardInterrupt);
	g_main_loop_quit(main_loop);
    }

    pyglib_gil_state_release(state);

    return FALSE;
}

static gboolean
pyg_signal_watch_dispatch(GSource     *source,
			  GSourceFunc  callback,
			  gpointer     user_data)
{
    /* We should never be dispatched */
    g_assert_not_reached();
    return TRUE;
}

static GSourceFuncs pyg_signal_watch_funcs =
{
    pyg_signal_watch_prepare,
    pyg_signal_watch_check,
    pyg_signal_watch_dispatch
};

static GSource *
pyg_signal_watch_new(void)
{
    GSource *source = g_source_new(&pyg_signal_watch_funcs,
	sizeof(PySignalWatchSource));

#ifdef HAVE_PYSIGNAL_SETWAKEUPFD
    PySignalWatchSource *real_source = (PySignalWatchSource *)source;
    int flag;

    /* Unfortunately we need to create a new pipe here instead of
     * reusing the pipe inside the GMainContext.
     * Ideally an api should be added to GMainContext which allows us
     * to reuse that pipe which would suit us perfectly fine.
     * XXX More efficient than a pipe, we could use an eventfd on Linux
     * kernels that support it.
     */
    gint already_piped = (pipe_fds[0] > 0);
    if (!already_piped) {
	if (pipe(pipe_fds) < 0)
	    g_error("Cannot create main loop pipe: %s\n",
	            g_strerror(errno));

        /* Make the write end of the fd non blocking */
        flag = fcntl(pipe_fds[1], F_GETFL, 0);
        flag |= O_NONBLOCK;
        fcntl(pipe_fds[1], F_SETFL, flag);
    }

    real_source->fd.fd = pipe_fds[0];
    real_source->fd.events = G_IO_IN | G_IO_HUP | G_IO_ERR;
    g_source_add_poll(source, &real_source->fd);

    if (!already_piped)
      PySignal_SetWakeupFd(pipe_fds[1]);
#endif
    return source;
}

PYGLIB_DEFINE_TYPE("glib.MainLoop", PyGMainLoop_Type, PyGMainLoop)

static int
pyg_main_loop_init(PyGMainLoop *self, PyObject *args, PyObject *kwargs)
{
    static char *kwlist[] = { "context", "is_running", NULL };
    PyObject *py_context = Py_None;
    int is_running = 0;
    GMainContext *context;
    
    if (!PyArg_ParseTupleAndKeywords(args, kwargs,
				     "|Ob:GMainLoop.__init__",
				     kwlist, &py_context, &is_running))
        return -1;

    if (!PyObject_TypeCheck(py_context, &PyGMainContext_Type) &&
	py_context != Py_None) {
	PyErr_SetString(PyExc_TypeError,
			"context must be a glib.MainContext or None");
	return -1;
    }

    if (py_context != Py_None) {
	context = ((PyGMainContext*)py_context)->context;
    } else {
	context = NULL;
    }

    self->loop = g_main_loop_new(context, is_running);

    self->signal_source = pyg_signal_watch_new();
    g_source_attach(self->signal_source, context);
    g_source_unref(self->signal_source);

    return 0;
}

static void
pyg_main_loop_dealloc(PyGMainLoop *self)
{
    if (self->signal_source != NULL) {
	g_source_destroy(self->signal_source);
	self->signal_source = NULL;
    }

    if (self->loop != NULL) {
	g_main_loop_unref(self->loop);
	self->loop = NULL;
    }

    PyObject_Del(self);
}

static PyObject*
pyg_main_loop_richcompare(PyObject *self, PyObject *other, int op)
{
    if (Py_TYPE(self) == Py_TYPE(other) && Py_TYPE(self) == &PyGMainLoop_Type)
        return _pyglib_generic_ptr_richcompare(((PyGMainLoop*)self)->loop,
                                               ((PyGMainLoop*)other)->loop,
                                               op);
    else {
       Py_INCREF(Py_NotImplemented);
       return Py_NotImplemented;
    }
}

static PyObject *
_wrap_g_main_loop_get_context (PyGMainLoop *loop)
{
    return pyg_main_context_new(g_main_loop_get_context(loop->loop));
}

static PyObject *
_wrap_g_main_loop_is_running (PyGMainLoop *self)
{
    return PyBool_FromLong(g_main_loop_is_running(self->loop));
}

static PyObject *
_wrap_g_main_loop_quit (PyGMainLoop *self)
{
    g_main_loop_quit(self->loop);
    
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
_wrap_g_main_loop_run (PyGMainLoop *self)
{
    GMainLoop *prev_loop;

    prev_loop = pyg_save_current_main_loop(self->loop);

    pyglib_begin_allow_threads;
    g_main_loop_run(self->loop);
    pyglib_end_allow_threads;

    pyg_restore_current_main_loop(prev_loop);
   
    if (PyErr_Occurred())
        return NULL;

    Py_INCREF(Py_None);
    return Py_None;
}

static PyMethodDef _PyGMainLoop_methods[] = {
    { "get_context", (PyCFunction)_wrap_g_main_loop_get_context, METH_NOARGS },
    { "is_running", (PyCFunction)_wrap_g_main_loop_is_running, METH_NOARGS },
    { "quit", (PyCFunction)_wrap_g_main_loop_quit, METH_NOARGS },
    { "run", (PyCFunction)_wrap_g_main_loop_run, METH_NOARGS },
    { NULL, NULL, 0 }
};

void
pyglib_mainloop_register_types(PyObject *d)
{
    PyGMainLoop_Type.tp_dealloc = (destructor)pyg_main_loop_dealloc;
    PyGMainLoop_Type.tp_richcompare = pyg_main_loop_richcompare;
    PyGMainLoop_Type.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE;
    PyGMainLoop_Type.tp_methods = _PyGMainLoop_methods;
    PyGMainLoop_Type.tp_init = (initproc)pyg_main_loop_init;
    PYGLIB_REGISTER_TYPE(d, PyGMainLoop_Type, "MainLoop"); 
}
