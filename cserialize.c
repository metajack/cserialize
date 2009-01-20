#include "Python.h"

#if PY_VERSION_HEX < 0x02050000
typedef int Py_ssize_t;
#define PY_SSIZE_T_MAX INT_MAX
#define PY_SSIZE_T_MIN INT_MIN
#endif

struct prefix_st {
    char *uri;
    char *prefix;
    int in_scope;
    int needs_write;
    struct prefix_st *next;
};

typedef struct prefix_st prefix_t;

static int prefix_counter = 0;

static prefix_t *prefix_new(void)
{
    prefix_t *list;

    list = (prefix_t *)malloc(sizeof(prefix_t));
    if (list) {
        list->uri = NULL;
        list->prefix = NULL;
        list->in_scope = 0;
        list->needs_write = 0;
        list->next = NULL;
    }

    return list;
}

static void prefix_free(prefix_t *list)
{
    prefix_t *next;

    if (!list) return;

    while (list) {
        next = list->next;
        if (list->prefix) free(list->prefix);
        if (list->uri) free(list->uri);
        free(list);
        list = next;
    }
}

static prefix_t *prefix_create(char *uri)
{
    prefix_t *item;
    char buf[128];

    item = prefix_new();
    snprintf(buf, 127, "ns%d", prefix_counter++);
    item->prefix = strdup(buf);
    item->uri = strdup(uri);
    return item;
}

static void prefix_append(prefix_t *list, prefix_t *item)
{
    while (list->next) list = list->next;
    list->next = item;
}

static prefix_t *prefix_find_uri(prefix_t **list, char *uri)
{
    prefix_t *item;
    prefix_t *curr = *list;

    while (curr) {
        if (strcmp(uri, curr->uri) == 0)
            return curr;
        curr = curr->next;
    }

    item = prefix_create(uri);
    if (!item) return NULL;

    if (*list == NULL) {
        *list = item;
    } else {
        prefix_append(*list, item);
    }

    return item;
}

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

static int do_serialize(PyObject *element,
                        char *defaultNS, prefix_t *prefixes, int closeElement,
                        char *buf, int pos, int len)
{
    PyObject *o;
    char *name, *s, *uri_s;
    int size, total, namesize, i, ret, keysize, prefixsize, valsize;
    PyObject *elemname, *attrs, *key, *value, *children, *child, *uri;
    PyObject *keyns, *keyname;
    Py_ssize_t dictpos = 0;
    int namedecref = 0, uridecref = 0, uri2decref = 0;
    int decref = 0;
    int decref2 = 0;
    int decref3 = 0;
    int writeDefaultNS = 0;
    prefix_t *prefix = NULL;
    prefix_t *nameprefix = NULL;
    prefix_t *attrprefix = NULL;

    ret = 0;
    uri = NULL;
    elemname = NULL;

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

    /* namespaces */
    /* we have to handle these at the beginning because we may have to 
     * put a prefix on the element name */
    if (!PyObject_HasAttrString(element, "defaultUri")) {
        ret = -1;
        goto error;
    }

    uri = PyObject_GetAttrString(element, "defaultUri");
    if (uri != Py_None && !PyString_Check(uri) && !PyUnicode_Check(uri)) {
        ret = -1;
        goto error;
    }

    if (PyString_Check(uri) || PyUnicode_Check(uri)) {
        if (PyUnicode_Check(uri)) {
            uri = PyUnicode_AsUTF8String(uri);
            uridecref = 1;
        }

        uri_s = PyString_AsString(uri);
        size = PyString_Size(uri);

        if (!defaultNS || strcmp(defaultNS, uri_s) != 0) {
            defaultNS = uri_s;
            writeDefaultNS = size;
        }
    }

    /* prefixes */
    if (!PyObject_HasAttrString(element, "uri")) {
        ret = -1;
        goto error;
    }

    uri = PyObject_GetAttrString(element, "uri");
    if (uri != Py_None && !PyString_Check(uri) && !PyUnicode_Check(uri)) {
        ret = -1;
        goto error;
    }

    if (PyString_Check(uri) || PyUnicode_Check(uri)) {
        if (PyUnicode_Check(uri)) {
            uri = PyUnicode_AsUTF8String(uri);
            uri2decref = 1;
        }
        
        uri_s = PyString_AsString(uri);
        if (strcmp(uri_s, defaultNS) != 0) {
            prefix = prefix_find_uri(&prefixes, uri_s);
            if (!prefix->in_scope) {
                prefix->needs_write = 1;
                prefix->in_scope = 1;
            }
            nameprefix = prefix;
        }

        if (uri2decref) {
            Py_DECREF(uri);
            uri2decref = 0;
        }
    }

    if (!PyObject_HasAttrString(element, "name")) {
        ret = -1;
        goto error;
    }

    elemname = PyObject_GetAttrString(element, "name");
    if (PyUnicode_Check(elemname)) {
        elemname = PyUnicode_AsUTF8String(elemname);
        namedecref = 1;
    } else if (!PyString_Check(elemname)) {
        ret = -1;
        goto error;
    }
    
    name = PyString_AsString(elemname);
    namesize = PyString_Size(elemname);

    if (nameprefix) {
        size = strlen(nameprefix->prefix);
        if ((namesize + size + 2) > (len - pos))
            goto error;
        
        buf[pos++] = '<';
        strcpy(&buf[pos], nameprefix->prefix);
        pos += size;
        buf[pos++] = ':';
        strcpy(&buf[pos], name);
        pos += namesize;
    } else {
        if (namesize + 1 > (len - pos))
            goto error;

        buf[pos++] = '<';
        strcpy(&buf[pos], name);
        pos += namesize;
    }
    
    /* attributes */
    if (!PyObject_HasAttrString(element, "attributes")) {
        ret = -1;
        goto error;
    }

    attrs = PyObject_GetAttrString(element, "attributes");
    if (!PyDict_Check(attrs)) {
        ret = -1;
        goto error;
    }

    while (PyDict_Next(attrs, &dictpos, &key, &value)) {
        attrprefix = NULL;

        if (!PyString_Check(key) && !PyUnicode_Check(key) &&
            !PyTuple_Check(key)) {
            ret = -1;
            goto error;
        }

        if (!PyString_Check(value) && !PyUnicode_Check(value)) {
            ret = -1;
            goto error;
        }
        
        if (PyTuple_Check(key)) {
            if (PyTuple_GET_SIZE(key) != 2) {
                ret = -1;
                goto error;
            }

            keyns = PyTuple_GET_ITEM(key, 0);
            keyname = PyTuple_GET_ITEM(key, 1);

            if (!PyString_Check(keyns) && !PyUnicode_Check(keyns)) {
                ret = -1;
                goto error;
            }

            if (!PyString_Check(keyname) && !PyUnicode_Check(keyname)) {
                ret = -1;
                goto error;
            }

            /* these decrefs have to match the other branch */
            if (PyUnicode_Check(keyns)) {
                keyns = PyUnicode_AsUTF8String(keyns);
                decref3 = 1;
            }

            if (PyUnicode_Check(keyname)) {
                keyname = PyUnicode_AsUTF8String(keyname);
                decref = 1;
            }

            if (PyUnicode_Check(value)) {
                value = PyUnicode_AsUTF8String(value);
                decref2 = 1;
            }

            key = keyname;
            attrprefix = prefix_find_uri(&prefixes, PyString_AsString(keyns));
            if (!attrprefix->in_scope) attrprefix->needs_write = 1;

            if (decref3) {
                Py_DECREF(keyns);
                decref3 = 0;
            }
        } else {
            if (PyUnicode_Check(key)) {
                key = PyUnicode_AsUTF8String(key);
                decref = 1;
            }
            
            if (PyUnicode_Check(value)) {
                value = PyUnicode_AsUTF8String(value);
                decref2 = 1;
            }
        }

        keysize = PyString_Size(key);
        valsize = PyString_Size(value);
        if (!attrprefix) {
            total = keysize + valsize;
            if (total + 4 > (len - pos)) {
                if (decref) {
                    Py_DECREF(key);
                    decref = 0;
                }
                if (decref2) {
                    Py_DECREF(value);
                    decref2 = 0;
                }
                goto error;
            }

            buf[pos++] = ' ';
            strcpy(&buf[pos], PyString_AsString(key));
            pos += keysize;
        } else {
            prefixsize = strlen(attrprefix->prefix);
            total = keysize + valsize + prefixsize;
            if ((total + 5) > (len - pos)) {
                if (decref) {
                    Py_DECREF(key);
                    decref = 0;
                }
                if (decref2) {
                    Py_DECREF(value);
                    decref2 = 0;
                }

                goto error;
            }
            buf[pos++] = ' ';
            strcpy(&buf[pos], attrprefix->prefix);
            pos += prefixsize;
            buf[pos++] = ':';
            strcpy(&buf[pos], PyString_AsString(key));
            pos += keysize;
        }
        buf[pos++] = '=';
        buf[pos++] = '\'';
        pos = encode(PyString_AsString(value), valsize, 
                     buf, pos, len);
        if (!pos) {
            if (decref) {
                Py_DECREF(key);
                decref = 0;
            }
            if (decref2) {
                Py_DECREF(value);
                decref2 = 0;
            }
            
            goto error;
        }
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

    /* write out namespaces and prefixes */
    if (writeDefaultNS) {
        if ((writeDefaultNS + 9) > (len - pos))
            goto error;
        strcpy(&buf[pos], " xmlns='");
        pos += 8;
        strcpy(&buf[pos], defaultNS);
        pos += writeDefaultNS;
        buf[pos++] = '\'';
    }

    for (prefix = prefixes; prefix; prefix = prefix->next) {
        if (prefix->needs_write && strcmp(prefix->prefix, "xml")) {
            prefix->needs_write = 0;
            prefix->in_scope = 1;

            size = strlen(prefix->prefix);
            total = size + strlen(prefix->uri);
            if ((total + 10) > len - pos)
                goto error;
            strcpy(&buf[pos], " xmlns:");
            pos += 7;
            strcpy(&buf[pos], prefix->prefix);
            pos += size;
            buf[pos++] = '=';
            buf[pos++] = '\'';
            strcpy(&buf[pos], prefix->uri);
            pos += total - size;
            buf[pos++] = '\'';
        }
    }
        
    /* short circuit if closeElement is false */
    if (!closeElement) {
        if (1 > (len - pos))
            goto error;
        buf[pos++] = '>';
        
        /* this isn't an error, but the exit teardown is the same */
        ret = pos;
        goto error;
    }

    /* children */
    if (!PyObject_HasAttrString(element, "children")) {
        ret = -1;
        goto error;
    }

    children = PyObject_GetAttrString(element, "children");
    if (!PyList_Check(children)) {
        ret = -1;
        goto error;
    }

    size = PyList_Size(children);
    if (size > 0) {
        if (1 > (len - pos))
            goto error;
        buf[pos++] = '>';

        for (i = 0; i < size; i++) {
            child = PyList_GET_ITEM(children, i);
            pos = do_serialize(child, defaultNS, prefixes, closeElement,
                               buf, pos, len);
            if (!pos)
                goto error;
        }

        if (nameprefix) {
            size = strlen(nameprefix->prefix);
            if ((namesize + size + 4) > (len - pos))
                goto error;
            
            buf[pos++] = '<';
            buf[pos++] = '/';
            strcpy(&buf[pos], nameprefix->prefix);
            pos += size;
            buf[pos++] = ':';
            strcpy(&buf[pos], name);
            pos += namesize;
            buf[pos++] = '>';
        } else {
            if (namesize + 3 > (len - pos))
                goto error;

            buf[pos++] = '<';
            buf[pos++] = '/';
            strcpy(&buf[pos], name);
            pos += namesize;
            buf[pos++] = '>';
        }
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
    if (uridecref) {
        Py_DECREF(uri);
        uridecref = 0;
    }

    return ret;
}

static int convert_from_dict(PyObject *dict, prefix_t **list)
{
    PyObject *key, *value;
    Py_ssize_t dpos;
    prefix_t *item;
    int kdecref = 0;
    int vdecref = 0;
    prefix_t *prefixes = NULL;

    /* convert prefix dict to internal list structure */
    if (dict) {
        if (dict != Py_None && !PyDict_Check(dict)) {
            PyErr_SetString(PyExc_TypeError,
                            "prefixes kwarg must be a dictionary");
            return -1;
        }

        if (PyDict_Check(dict)) {
            while (PyDict_Next(dict, &dpos, &key, &value)) {
                if (!PyString_Check(key) && !PyUnicode_Check(key)) {
                    PyErr_SetString(PyExc_TypeError,
                                    "prefixes must be strings");
                    return -1;
                }
                
                if (!PyString_Check(value) && !PyUnicode_Check(value)) {
                    PyErr_SetString(PyExc_TypeError,
                                    "prefixes must be strings");
                    return -1;
                }
                
                if (PyUnicode_Check(key)) {
                    key = PyUnicode_AsUTF8String(key);
                    kdecref = 1;
                }
                
                if (PyUnicode_Check(value)) {
                    value = PyUnicode_AsUTF8String(value);
                    vdecref = 1;
                }
                
                item = prefix_new();
                if (!item) {
                    if (kdecref) {
                        Py_DECREF(key);
                        kdecref = 0;
                    }
                    if (vdecref) {
                        Py_DECREF(value);
                        vdecref = 0;
                    }
                    
                    PyErr_SetString(PyExc_RuntimeError, "out of memory");
                    return -1;
                }
                
                item->uri = strdup(PyString_AsString(key));
                item->prefix = strdup(PyString_AsString(value));
                
                if (prefixes)
                    prefix_append(prefixes, item);
                else
                    prefixes = item;
                
                if (kdecref) {
                    Py_DECREF(key);
                    kdecref = 0;
                }
                if (vdecref) {
                    Py_DECREF(value);
                    vdecref = 0;
                }
            }
        }
    }

    if (prefixes)
        *list = prefixes;
    return 0;
}

PyDoc_STRVAR(serialize__doc__,
             "Serialize a domish element.");

static PyObject *serialize(PyObject *self, PyObject *args, PyObject *kwargs)
{
    int ok, size, i;
    PyObject *element, *result, *value;
    char buf[4096], *dynbuf;
    prefix_t *found;
    int len = 4096;
    int closeElement = 1;
    int decref = 0;
    PyObject *prefixdict = NULL;
    PyObject *defaultUri = NULL;
    PyObject *prefixesInScope = NULL;
    prefix_t *prefixes = NULL;
    prefix_t *plist = NULL;

    static char *kwlist[] = {"element", "prefixes", "closeElement", 
                             "defaultUri", "prefixesInScope", NULL};

    ok = PyArg_ParseTupleAndKeywords(args, kwargs, "O|OiOO", kwlist,
                                     &element, &prefixdict, &closeElement,
                                     &defaultUri, &prefixesInScope);
    if (!ok) {
        PyErr_SetString(PyExc_TypeError,
                        "serialize() takes exactly one or two arguments");
        return NULL;
    }

    prefixes = prefix_new();
    if (!prefixes) {
        PyErr_SetString(PyExc_RuntimeError,
                        "memory allocation failed");
        return NULL;
    }

    prefixes->uri = strdup("http://www.w3.org/XML/1998/namespace");
    prefixes->prefix = strdup("xml");

    /* convert prefix dict to internal list structure */
    plist = NULL;
    ok = convert_from_dict(prefixdict, &plist);
    if (ok < 0) return NULL;
    prefixes->next = plist;

    if (prefixesInScope) {
        if (prefixesInScope != Py_None && !PyList_Check(prefixesInScope)) {
            PyErr_SetString(PyExc_TypeError,
                            "Expected list or none for prefixesInScope.");
            return NULL;
        }
        if (PyList_Check(prefixesInScope)) {
            for (i = 0; i < PyList_GET_SIZE(prefixesInScope); i++) {
                value = PyList_GET_ITEM(prefixesInScope, i);
                if (!PyString_Check(value) && !PyUnicode_Check(value)) {
                    PyErr_SetString(PyExc_TypeError,
                                    "Expected strings in prefixesInScope.");
                    return NULL;
                }

                if (PyUnicode_Check(value)) {
                    value = PyUnicode_AsUTF8String(value);
                    decref = 1;
                }

                for (found = prefixes; found; found = found->next)
                    if (strcmp(PyString_AsString(value), found->prefix) == 0)
                        break;
                
                if (found)
                    found->in_scope = 1;
                
                if (decref) {
                    Py_DECREF(value);
                    decref = 0;
                }
            }
        }
    }

    dynbuf = NULL;
    size = do_serialize(element, NULL, prefixes, closeElement, buf, 0, len);
    while (size == 0) {
        if (dynbuf)
            free(dynbuf);
        len *= 2;
        dynbuf = (char *)malloc(len);
        if (!dynbuf) {
            if (prefixes) prefix_free(prefixes);
            Py_RETURN_NONE;
        }
        size = do_serialize(element, NULL, prefixes, closeElement, 
                            dynbuf, 0, len);
    }

    if (prefixes) prefix_free(prefixes);

    if (size < 0) {
        PyErr_SetString(PyExc_TypeError, "Incorrect object in element tree.");
        return NULL;
    }

    if (dynbuf) {
        dynbuf[size] = 0;
        result = PyUnicode_DecodeUTF8(dynbuf, size, NULL);
        free(dynbuf);
    } else {
        buf[size] = 0;
        result = PyUnicode_DecodeUTF8(buf, size, NULL);
    }

    return result;
}

static PyMethodDef cserialize_methods[] = {
    {"serialize", (PyCFunction)serialize, 
     METH_VARARGS | METH_KEYWORDS, serialize__doc__},
    {NULL, NULL}
};


PyDoc_STRVAR(cserialize__doc__,
             "Domish XML serializer in C.");

PyMODINIT_FUNC initcserialize(void)
{
    Py_InitModule3("cserialize", cserialize_methods, cserialize__doc__);
}
