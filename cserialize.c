#include "Python.h"

static int encode(char *val, int size, char *buf, int pos, int len)
{
    int c;

    for (c = 0; c < size; c++) {
        switch (val[c]) {
        case '&':
            if (5 > (len - pos))
                return 0;
            strncpy(&buf[pos], "&amp;", 5);
            pos += 5;
            break;
        case '<':
            if (4 > (len - pos))
                return 0;

            strncpy(&buf[pos], "&lt;", 4);
            pos += 4;
            break;
        case '>':
            if (4 > (len - pos))
                return 0;
            strncpy(&buf[pos], "&gt;", 4);
            pos += 4;
            break;
        default:
            if (1 > (len - pos))
                return 0;
            buf[pos++] = val[c];
        }
    }

    return pos;
}

static int do_serialize(PyObject *element, char *buf, int pos, int len)
{
    PyObject *o;
    char *name, *s;
    int size, total, namesize, i;
    PyObject *elemname, *attrs, *key, *value, *children, *child;
    Py_ssize_t dictpos = 0;
    int namedecref = 0;
    int decref = 0;
    int decref2 = 0;

    /* handle content */
    if (PyString_Check(element) || PyUnicode_Check(element)) {
        if (PyUnicode_Check(element)) {
            o = PyUnicode_AsUTF8String(element);
            decref = 1;
        } else
            o = element;

        s = PyString_AsString(o);
        size = PyString_Size(o);
        pos = encode(s, size, buf, pos, len);
        if (!pos)
            goto error;

        if (decref) {
            Py_DECREF(o);
            decref = 0;
        }

        return pos;
    }

    /* handle elements */
    if (!PyObject_HasAttrString(element, "name"))
        goto error;

    elemname = PyObject_GetAttrString(element, "name");
    if (PyUnicode_Check(elemname)) {
        elemname = PyUnicode_AsUTF8String(elemname);
        namedecref = 1;
    } else if (!PyString_Check(elemname)) {
        goto error;
    }
    
    name = PyString_AsString(elemname);
    namesize = PyString_Size(elemname);

    if (namesize + 1 > (len - pos))
        goto error;

    buf[pos++] = '<';
    strcpy(&buf[pos], name);
    pos += namesize;

    /* attributes */
    if (!PyObject_HasAttrString(element, "attributes"))
        goto error;

    attrs = PyObject_GetAttrString(element, "attributes");
    if (!PyDict_Check(attrs))
        goto error;

    while (PyDict_Next(attrs, &dictpos, &key, &value)) {
        if (!PyString_Check(key) && !PyUnicode_Check(key))
            goto error;

        if (!PyString_Check(value) && !PyUnicode_Check(value))
            goto error;
        
        if (PyUnicode_Check(key)) {
            key = PyUnicode_AsUTF8String(key);
            decref = 1;
        }

        if (PyUnicode_Check(value)) {
            value = PyUnicode_AsUTF8String(value);
            decref2 = 1;
        }

        size = PyString_Size(key);
        total = size + PyString_Size(value);
        if (total + 4 > (len - pos))
            goto error;

        buf[pos++] = ' ';
        strcpy(&buf[pos], PyString_AsString(key));
        pos += size;
        buf[pos++] = '=';
        buf[pos++] = '\'';
        pos = encode(PyString_AsString(value), total - size, 
                     buf, pos, len);
        if (!pos)
            goto error;
        buf[pos++] = '\'';

        if (decref) {
            Py_DECREF(key);
            decref = 0;
        }

        if (decref2) {
            Py_DECREF(value);
            decref2 = 0;
        }
    }

    /* children */
    if (!PyObject_HasAttrString(element, "children"))
        goto error;

    children = PyObject_GetAttrString(element, "children");
    if (!PyList_Check(children))
        goto error;

    size = PyList_Size(children);
    if (size > 0) {
        if (1 > (len - pos))
            goto error;
        buf[pos++] = '>';

        for (i = 0; i < size; i++) {
            child = PyList_GET_ITEM(children, i);
            pos = do_serialize(child, buf, pos, len);
            if (!pos)
                goto error;
        }

        if (namesize + 3 > (len - pos))
            goto error;

        buf[pos++] = '<';
        buf[pos++] = '/';
        strcpy(&buf[pos], name);
        pos += namesize;
        buf[pos++] = '>';
    } else {
        if (2 > (len - pos))
            goto error;
        buf[pos++] = '/';
        buf[pos++] = '>';
    }

    return pos;

error:
    if (namedecref) {
        Py_DECREF(elemname);
        namedecref = 0;
    }

    return 0;
}


PyDoc_STRVAR(serialize__doc__,
             "Serialize a domish element.");

static PyObject *serialize(PyObject *self, PyObject *args)
{
    int ok, size;
    PyObject *element;
    char buf[4096];
    int len = 4096;

    ok = PyArg_ParseTuple(args, "O", &element);
    if (!ok) 
        Py_RETURN_NONE;

    size = do_serialize(element, buf, 0, len);
    if (!size) {
        Py_RETURN_NONE;
    }

    buf[size] = 0;
    return PyUnicode_DecodeUTF8(buf, size, NULL);
}

static PyMethodDef cserialize_methods[] = {
    {"serialize", serialize, METH_VARARGS, serialize__doc__},
    {NULL, NULL}
};


PyDoc_STRVAR(cserialize__doc__,
             "Domish XML serializer in C.");

PyMODINIT_FUNC initcserialize(void)
{
    Py_InitModule3("cserialize", cserialize_methods, cserialize__doc__);
}
