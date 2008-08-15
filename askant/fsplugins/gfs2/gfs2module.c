#include <Python.h>

#include "gfs2.h"

static PyObject *report_func = NULL;

void report_func_wrapper(long int blk, char *type, long int parent, char *fn)
{
	PyObject *arglist;
	PyObject *result;

	arglist = Py_BuildValue("lsls", blk, type, parent, fn);
	if (!arglist) {
		return;
	}
	result = PyEval_CallObject(report_func, arglist);
	Py_DECREF(arglist);
	if (!result) {
		return;
	}
	Py_DECREF(result);
}

static PyObject *gfs2_set_report_hook(PyObject *self, PyObject *args)
{
	PyObject *result = NULL;
	PyObject *temp;

	if (PyArg_ParseTuple(args, "O:set_callback", &temp)) {
		if (!PyCallable_Check(temp)) {
			PyErr_SetString(PyExc_TypeError, "parameter must be callable");
			return NULL;
		}
		Py_XINCREF(temp);
		Py_XDECREF(report_func);
		report_func = temp;

		Py_INCREF(Py_None);
		result = Py_None;
	}
	return result;
}

static PyObject *gfs2_parsefs(PyObject *self, PyObject *args)
{
	char *dev;

	if (!PyArg_ParseTuple(args, "s:parsefs", &dev)) {
		return NULL;
	}

	if (!gfs2_parse(dev, &report_func_wrapper)) {
		return PyErr_SetFromErrno(PyExc_IOError);
	}
	Py_INCREF(Py_None);

	return Py_None;
}

static PyObject *gfs2_get_block_size(PyObject *self, PyObject *args)
{
	char *dev;
	uint32_t blksize;

	if (!PyArg_ParseTuple(args, "s:get_block_size", &dev)) {
		return NULL;
	}

	blksize = gfs2_block_size(dev);
	if (!blksize) {
		return PyErr_SetFromErrno(PyExc_IOError);
	}

	PyObject *size = Py_BuildValue("I", blksize);
	if (!size) {
		return NULL;
	}
	Py_INCREF(size);
	return size;
}

static PyObject *gfs2_handle_sigint(PyObject *signum, PyObject *frame)
{
	gfs2_stop();
	Py_INCREF(Py_None);
	return Py_None;
}

static PyMethodDef GFS2Methods[] = {
	{"set_report_hook", gfs2_set_report_hook, METH_VARARGS,
		"Specify a hook function through which to report blocks."},
	{"parsefs", gfs2_parsefs, METH_VARARGS,
		"Parses the given block device as a GFS2 file system."},
	{"get_block_size", gfs2_get_block_size, METH_VARARGS,
		"Returns the file system block size."},
	{"handle_sigint", gfs2_handle_sigint, METH_VARARGS,
		"Handles signal SIGINT."},
	{NULL, NULL, 0, NULL}
};

PyMODINIT_FUNC initgfs2(void)
{
    (void)Py_InitModule("gfs2", GFS2Methods);
}
