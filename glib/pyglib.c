/* -*- Mode: C; c-set-style: python; c-basic-offset: 4  -*-
 * pyglib - Python bindings for GLib toolkit.
 * Copyright (C) 1998-2003  James Henstridge
 *               2004-2008  Johan Dahlin
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

#include <Python.h>
#include <pythread.h>
#include "pyglib.h"
#include "pyglib-private.h"
#include "pygmaincontext.h"
#include "pygoptioncontext.h"
#include "pygoptiongroup.h"

static struct _PyGLib_Functions *_PyGLib_API;
static int pyglib_thread_state_tls_key;
static PyObject *exception_table = NULL;

void
pyglib_init(void)
{
    PyObject *glib, *cobject;
    
    glib = PyImport_ImportModule("glib");
    if (!glib) {
	if (PyErr_Occurred()) {
	    PyObject *type, *value, *traceback;
	    PyObject *py_orig_exc;
	    PyErr_Fetch(&type, &value, &traceback);
	    py_orig_exc = PyObject_Repr(value);
	    Py_XDECREF(type);
	    Py_XDECREF(value);
	    Py_XDECREF(traceback);
	    PyErr_Format(PyExc_ImportError,
			 "could not import glib (error was: %s)",
			 PYGLIB_PyUnicode_AsString(py_orig_exc));
	    Py_DECREF(py_orig_exc);
        } else {
	    PyErr_SetString(PyExc_ImportError,
			    "could not import glib (no error given)");
	}
	return;
    }
    
    cobject = PyObject_GetAttrString(glib, "_PyGLib_API");
    if (cobject && PYGLIB_CPointer_Check(cobject))
	_PyGLib_API = (struct _PyGLib_Functions *) PYGLIB_CPointer_GetPointer(cobject, "glib._PyGLib_API");
    else {
	PyErr_SetString(PyExc_ImportError,
			"could not import glib (could not find _PyGLib_API object)");
	Py_DECREF(glib);
	return;
    }
}

void
pyglib_init_internal(PyObject *api)
{
    _PyGLib_API = (struct _PyGLib_Functions *) PYGLIB_CPointer_GetPointer(api, "glib._PyGLib_API");
}

gboolean
pyglib_threads_enabled(void)
{
    g_return_val_if_fail (_PyGLib_API != NULL, FALSE);

    return _PyGLib_API->threads_enabled;
}

PyGILState_STATE
pyglib_gil_state_ensure(void)
{
    g_return_val_if_fail (_PyGLib_API != NULL, PyGILState_LOCKED);

    if (!_PyGLib_API->threads_enabled)
	return PyGILState_LOCKED;

#ifdef DISABLE_THREADING
    return PyGILState_LOCKED;
#else
    return PyGILState_Ensure();
#endif
}

void
pyglib_gil_state_release(PyGILState_STATE state)
{
    g_return_if_fail (_PyGLib_API != NULL);

    if (!_PyGLib_API->threads_enabled)
	return;

#ifndef DISABLE_THREADING
    PyGILState_Release(state);
#endif
}

/**
 * pyglib_enable_threads:
 *
 * Returns: TRUE if threading is enabled, FALSE otherwise.
 *
 */
#ifdef DISABLE_THREADING
gboolean
pyglib_enable_threads(void)
{
    PyErr_SetString(PyExc_RuntimeError,
		    "pyglib threading disabled at compile time");
    return FALSE;
}

void
_pyglib_notify_on_enabling_threads(PyGLibThreadsEnabledFunc callback)
{
    /* Ignore, threads cannot be enabled. */
}

#else

static GSList *thread_enabling_callbacks = NULL;

/* Enable threading; note that the GIL must be held by the current
 * thread when this function is called
 */
gboolean
pyglib_enable_threads(void)
{
    GSList *callback;

    g_return_val_if_fail (_PyGLib_API != NULL, FALSE);

    if (_PyGLib_API->threads_enabled)
	return TRUE;
  
    PyEval_InitThreads();
    
    _PyGLib_API->threads_enabled = TRUE;
    pyglib_thread_state_tls_key = PyThread_create_key();

    for (callback = thread_enabling_callbacks; callback; callback = callback->next)
	((PyGLibThreadsEnabledFunc) callback->data) ();

    g_slist_free(thread_enabling_callbacks);
    return TRUE;
}

void
_pyglib_notify_on_enabling_threads(PyGLibThreadsEnabledFunc callback)
{
    if (callback && !pyglib_threads_enabled())
	thread_enabling_callbacks = g_slist_append(thread_enabling_callbacks, callback);
}
#endif

int
pyglib_gil_state_ensure_py23 (void)
{
#ifdef DISABLE_THREADING
    return 0;
#else
    return PyGILState_Ensure();
#endif
}

void
pyglib_gil_state_release_py23 (int flag)
{
#ifndef DISABLE_THREADING
    PyGILState_Release(flag);
#endif
}

/**
 * pyglib_block_threads:
 *
 */
void
pyglib_block_threads(void)
{
    g_return_if_fail (_PyGLib_API != NULL);

    if (_PyGLib_API->block_threads != NULL)
	(* _PyGLib_API->block_threads)();
}

/**
 * pyglib_unblock_threads:
 *
 */
void
pyglib_unblock_threads(void)
{
    g_return_if_fail (_PyGLib_API != NULL);
    if (_PyGLib_API->unblock_threads != NULL)
	(* _PyGLib_API->unblock_threads)();
}

/**
 * pyglib_set_thread_block_funcs:
 *
 * hooks to register handlers for getting GDK threads to cooperate
 * with python threading
 */
void
pyglib_set_thread_block_funcs (PyGLibThreadBlockFunc block_threads_func,
			       PyGLibThreadBlockFunc unblock_threads_func)
{
    g_return_if_fail (_PyGLib_API != NULL);

    _PyGLib_API->block_threads = block_threads_func;
    _PyGLib_API->unblock_threads = unblock_threads_func;
}


/**
 * pyglib_error_check:
 * @error: a pointer to the GError.
 *
 * Checks to see if the GError has been set.  If the error has been
 * set, then the glib.GError Python exception will be raised, and
 * the GError cleared.
 *
 * Returns: True if an error was set.
 */
gboolean
pyglib_error_check(GError **error)
{
    PyGILState_STATE state;
    PyObject *exc_type;
    PyObject *exc_instance;
    PyObject *d;

    g_return_val_if_fail(error != NULL, FALSE);

    if (*error == NULL)
	return FALSE;
    
    state = pyglib_gil_state_ensure();

    exc_type = _PyGLib_API->gerror_exception;
    if (exception_table != NULL)
    {
	PyObject *item;
	item = PyDict_GetItem(exception_table, PYGLIB_PyLong_FromLong((*error)->domain));
	if (item != NULL)
	    exc_type = item;
    }

    exc_instance = PyObject_CallFunction(exc_type, "z", (*error)->message);

    if ((*error)->domain) {
	PyObject_SetAttrString(exc_instance, "domain",
			       d=PYGLIB_PyUnicode_FromString(g_quark_to_string((*error)->domain)));
	Py_DECREF(d);
    }
    else
	PyObject_SetAttrString(exc_instance, "domain", Py_None);

    PyObject_SetAttrString(exc_instance, "code",
			   d=PYGLIB_PyLong_FromLong((*error)->code));
    Py_DECREF(d);

    if ((*error)->message) {
	PyObject_SetAttrString(exc_instance, "message",
			       d=PYGLIB_PyUnicode_FromString((*error)->message));
	Py_DECREF(d);
    } else {
	PyObject_SetAttrString(exc_instance, "message", Py_None);
    }
    
    PyErr_SetObject(_PyGLib_API->gerror_exception, exc_instance);
    Py_DECREF(exc_instance);
    g_clear_error(error);
    
    pyglib_gil_state_release(state);
    
    return TRUE;
}

/**
 * pyglib_gerror_exception_check:
 * @error: a standard GLib GError ** output parameter
 *
 * Checks to see if a GError exception has been raised, and if so
 * translates the python exception to a standard GLib GError.  If the
 * raised exception is not a GError then PyErr_Print() is called.
 *
 * Returns: 0 if no exception has been raised, -1 if it is a
 * valid glib.GError, -2 otherwise.
 */
gboolean
pyglib_gerror_exception_check(GError **error)
{
    PyObject *type, *value, *traceback;
    PyObject *py_message, *py_domain, *py_code;
    const char *bad_gerror_message;

    PyErr_Fetch(&type, &value, &traceback);
    if (type == NULL)
        return 0;
    PyErr_NormalizeException(&type, &value, &traceback);
    if (value == NULL) {
        PyErr_Restore(type, value, traceback);
        PyErr_Print();
        return -2;
    }
    if (!value ||
	!PyErr_GivenExceptionMatches(type,
				     (PyObject *) _PyGLib_API->gerror_exception)) {
        PyErr_Restore(type, value, traceback);
        PyErr_Print();
        return -2;
    }
    Py_DECREF(type);
    Py_XDECREF(traceback);

    py_message = PyObject_GetAttrString(value, "message");
    if (!py_message || !PYGLIB_PyUnicode_Check(py_message)) {
        bad_gerror_message = "glib.GError instances must have a 'message' string attribute";
        goto bad_gerror;
    }

    py_domain = PyObject_GetAttrString(value, "domain");
    if (!py_domain || !PYGLIB_PyUnicode_Check(py_domain)) {
        bad_gerror_message = "glib.GError instances must have a 'domain' string attribute";
        Py_DECREF(py_message);
        goto bad_gerror;
    }

    py_code = PyObject_GetAttrString(value, "code");
    if (!py_code || !PYGLIB_PyLong_Check(py_code)) {
        bad_gerror_message = "glib.GError instances must have a 'code' int attribute";
        Py_DECREF(py_message);
        Py_DECREF(py_domain);
        goto bad_gerror;
    }

    g_set_error(error, g_quark_from_string(PYGLIB_PyUnicode_AsString(py_domain)),
                PYGLIB_PyLong_AsLong(py_code), "%s", PYGLIB_PyUnicode_AsString(py_message));

    Py_DECREF(py_message);
    Py_DECREF(py_code);
    Py_DECREF(py_domain);
    return -1;

bad_gerror:
    Py_DECREF(value);
    g_set_error(error, g_quark_from_static_string("pyglib"), 0, "%s", bad_gerror_message);
    PyErr_SetString(PyExc_ValueError, bad_gerror_message);
    PyErr_Print();
    return -2;
}

/**
 * pyglib_register_exception_for_domain:
 * @name: name of the exception
 * @error_domain: error domain
 *
 * Registers a new glib.GError exception subclass called #name for
 * a specific #domain. This exception will be raised when a GError
 * of the same domain is passed in to pyglib_error_check().
 *
 * Returns: the new exception
 */
PyObject *
pyglib_register_exception_for_domain(gchar *name,
				     gint error_domain)
{
    PyObject *exception;

    exception = PyErr_NewException(name, _PyGLib_API->gerror_exception, NULL);

    if (exception_table == NULL)
	exception_table = PyDict_New();

    PyDict_SetItem(exception_table,
		   PYGLIB_PyLong_FromLong(error_domain),
		   exception);
    
    return exception;
}

/**
 * pyglib_main_context_new:
 * @context: a GMainContext.
 *
 * Creates a wrapper for a GMainContext.
 *
 * Returns: the GMainContext wrapper.
 */
PyObject *
pyglib_main_context_new(GMainContext *context)
{
    return _PyGLib_API->main_context_new(context);
}

/**
 * pyg_option_group_transfer_group:
 * @group: a GOptionGroup wrapper
 *
 * This is used to transfer the GOptionGroup to a GOptionContext. After this
 * is called, the calle must handle the release of the GOptionGroup.
 *
 * When #NULL is returned, the GOptionGroup was already transfered.
 *
 * Returns: Either #NULL or the wrapped GOptionGroup.
 */
GOptionGroup *
pyglib_option_group_transfer_group(PyObject *obj)
{
    PyGOptionGroup *self = (PyGOptionGroup*)obj;
    
    if (self->is_in_context)
	return NULL;

    self->is_in_context = TRUE;
    
    /* Here we increase the reference count of the PyGOptionGroup, because now
     * the GOptionContext holds an reference to us (it is the userdata passed
     * to g_option_group_new().
     *
     * The GOptionGroup is freed with the GOptionContext.
     *
     * We set it here because if we would do this in the init method we would
     * hold two references and the PyGOptionGroup would never be freed.
     */
    Py_INCREF(self);
    
    return self->group;
}

/**
 * pyglib_option_group_new:
 * @group: a GOptionGroup
 *
 * The returned GOptionGroup can't be used to set any hooks, translation domains
 * or add entries. It's only intend is, to use for GOptionContext.add_group().
 *
 * Returns: the GOptionGroup wrapper.
 */
PyObject * 
pyglib_option_group_new (GOptionGroup *group)
{
    return _PyGLib_API->option_group_new(group);
}

/**
 * pyglib_option_context_new:
 * @context: a GOptionContext
 *
 * Returns: A new GOptionContext wrapper.
 */
PyObject * 
pyglib_option_context_new (GOptionContext *context)
{
    return _PyGLib_API->option_context_new(context);
}

/**
 * pyglib_option_context_new:
 * @context: a GTimeVal struct
 *
 * Converts a GTimeVal struct to a python float
 *
 * Returns: a float representing the timeval
 */
PyObject *
pyglib_float_from_timeval(GTimeVal timeval)
{
    double ret;
    ret = (double)timeval.tv_sec + (double)timeval.tv_usec * 0.000001;
    return PyFloat_FromDouble(ret);
}

/**
 * pyg_pystr_to_gfilename_conv:
 *
 * Converts PyObject value to a gchar * in GLib filename encoding. Follows the
 * calling convention of a ParseArgs converter (O& format specifier) so it may
 * be used to convert function arguments.
 *
 * Returns: 1 if the conversion succeeds and 0 otherwise.  If the conversion
 *          did not succeesd, a Python exception is raised
 */
int pyglib_pystr_to_gfilename_conv(PyObject *py_obj, void *ptr)
{
    gchar **filename = ptr;
    gchar *tmp;
#if PY_MAJOR_VERSION >= 3
    if (!PyUnicode_Check(py_obj)) {
        PyErr_SetString(PyExc_ValueError, "filename must be a string");
        return 0;
    }
#if defined(G_OS_WIN32)
    tmp = g_strdup(PyUnicode_AsUTF8(py_obj));
    if (tmp == NULL)
        return 0;
    *filename = tmp;
#else
    {
        PyObject *filename_bytes;
        if (!PyUnicode_FSConverter(py_obj, &filename_bytes))
            return 0;
        *filename = g_strdup(PyBytes_AS_STRING(filename_bytes));
        Py_DECREF(filename_bytes);
    }
#endif /* defined(G_OS_WIN32) */

#else /* PY_MAJOR_VERSION < 3 */
    if (!PyString_Check(py_obj)) {
        PyErr_SetString(PyExc_ValueError, "filename must be a string");
        return 0;
    }
    *filename = g_strdup(PyString_AS_STRING(py_obj));
#endif /* PY_MAJOR_VERSION >= 3 */
    return 1;
}

/**
 * pyg_pystr_to_gfilename:
 *
 * Converts PyObject value to a gchar * in GLib filename encoding.
 *
 * Returns: gchar * if the conversion succeeds and NULL otherwise.The caller is
 * needs to call g_free on the returned value when finished.
 */
gchar *
pyglib_pystr_to_gfilename(PyObject *py_obj)
{
    gchar *value = NULL;
    pyglib_pystr_to_gfilename_conv(py_obj, &value);
    return value;
}

PyObject *
pyglib_pystr_from_gfilename(const gchar *filename)
{
#if PY_MAJOR_VERSION >= 3

#if defined(G_OS_WIN32)
    return PyUnicode_FromString(filename);
#else
    return PyUnicode_DecodeFSDefault(filename);
#endif /* defined(G_OS_WIN32) */

#else
    return PYGLIB_PyString_FromString(filename);
#endif /* PY_MAJOR_VERSION >= 3 */
}


/****** Private *****/

/**
 * _pyglib_destroy_notify:
 * @user_data: a PyObject pointer.
 *
 * A function that can be used as a GDestroyNotify callback that will
 * call Py_DECREF on the data.
 */
void
_pyglib_destroy_notify(gpointer user_data)
{
    PyObject *obj = (PyObject *)user_data;
    PyGILState_STATE state;

    g_return_if_fail (_PyGLib_API != NULL);

    state = pyglib_gil_state_ensure();
    Py_DECREF(obj);
    pyglib_gil_state_release(state);
}

gboolean
_pyglib_handler_marshal(gpointer user_data)
{
    PyObject *tuple, *ret;
    gboolean res;
    PyGILState_STATE state;

    g_return_val_if_fail(user_data != NULL, FALSE);

    state = pyglib_gil_state_ensure();

    tuple = (PyObject *)user_data;
    ret = PyObject_CallObject(PyTuple_GetItem(tuple, 0),
			      PyTuple_GetItem(tuple, 1));
    if (!ret) {
	PyErr_Print();
	res = FALSE;
    } else {
	res = PyObject_IsTrue(ret);
	Py_DECREF(ret);
    }
    
    pyglib_gil_state_release(state);

    return res;
}

PyObject*
_pyglib_generic_ptr_richcompare(void* a, void *b, int op)
{
    PyObject *res;

    switch (op) {

      case Py_EQ:
        res = (a == b) ? Py_True : Py_False;
        break;

      case Py_NE:
        res = (a != b) ? Py_True : Py_False;
        break;

      case Py_LT:
        res = (a < b) ? Py_True : Py_False;
        break;

      case Py_LE:
        res = (a <= b) ? Py_True : Py_False;
        break;

      case Py_GT:
        res = (a > b) ? Py_True : Py_False;
        break;

      case Py_GE:
        res = (a >= b) ? Py_True : Py_False;
        break;

      default:
        res = Py_NotImplemented;
        break;
    }

    Py_INCREF(res);
    return res;
}

PyObject*
_pyglib_generic_long_richcompare(long a, long b, int op)
{
    PyObject *res;

    switch (op) {

      case Py_EQ:
        res = (a == b) ? Py_True : Py_False;
        Py_INCREF(res);
        break;

      case Py_NE:
        res = (a != b) ? Py_True : Py_False;
        Py_INCREF(res);
        break;


      case Py_LT:
        res = (a < b) ? Py_True : Py_False;
        Py_INCREF(res);
        break;

      case Py_LE:
        res = (a <= b) ? Py_True : Py_False;
        Py_INCREF(res);
        break;

      case Py_GT:
        res = (a > b) ? Py_True : Py_False;
        Py_INCREF(res);
        break;

      case Py_GE:
        res = (a >= b) ? Py_True : Py_False;
        Py_INCREF(res);
        break;

      default:
        res = Py_NotImplemented;
        Py_INCREF(res);
        break;
    }

    return res;
}

