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
    int stack_height;
    int needs_write;
    struct prefix_st *next;
};

typedef struct prefix_st prefix_t;

static prefix_t *prefix_new(void)
{
    prefix_t *list;

    list = (prefix_t *)malloc(sizeof(prefix_t));
    if (list) {
        list->uri = NULL;
        list->prefix = NULL;
        list->in_scope = 0;
        list->needs_write = 0;
        list->stack_height = 0;
        list->next = NULL;
    }

    return list;
}

static void prefix_free_list(prefix_t *list)
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

static void prefix_free(prefix_t *prefix) 
{
    if (!prefix) return;

    if (prefix->prefix) free(prefix->prefix);
    if (prefix->uri) free(prefix->uri);
    free(prefix);
}

static prefix_t *prefix_create(char *uri, int *counter)
{
    prefix_t *item;
    char buf[128];

    item = prefix_new();
    snprintf(buf, 127, "xn%d", (*counter)++);
    item->prefix = strdup(buf);
    item->uri = strdup(uri);
    return item;
}

static void prefix_append(prefix_t *list, prefix_t *item)
{
    while (list->next) list = list->next;
    list->next = item;
}

static prefix_t *prefix_find_uri(prefix_t **list, char *uri, int *counter)
{
    prefix_t *item;
    prefix_t *curr = *list;

    while (curr) {
        if (strcmp(uri, curr->uri) == 0)
            return curr;
        curr = curr->next;
    }

    item = prefix_create(uri, counter);
    if (!item) return NULL;

    if (*list == NULL) {
        *list = item;
    } else {
        prefix_append(*list, item);
    }

    return item;
}

static int encode(char *val, int size, int attr, char *buf, int pos, int len)
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
        case '\'':
            if (attr) {
                if (6 > (len - pos))
                    return 0;
                strncpy(&buf[pos], "&apos;", 6);
                pos += 6;
                break;
            }
        default:
            if (1 > (len - pos))
                return 0;
            buf[pos++] = val[c];
        }
    }

    return pos;
}

/* return a normal utf8 string from a unicode or string python object.
 * the original object may be destroyed, and the returned object must
 * be derefed at some point.
 */
static PyObject *make_utf8_string(PyObject *s)
{
    PyObject *result;

    if (PyUnicode_Check(s)) {
        result = PyUnicode_AsUTF8String(s);
        Py_DECREF(s);
    } else {
        result = s;
    }
    return result;
}

static int convert_from_dict(PyObject *dict, prefix_t **list)
{
    PyObject *key, *value;
    prefix_t *item;
    prefix_t *prefixes = NULL;
    Py_ssize_t dpos = 0;

    /* convert prefix dict to internal list structure */
    if (dict) {
        if (dict != Py_None && !PyDict_Check(dict)) {
            PyErr_SetString(PyExc_TypeError,
                            "expected a dictionary");
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
               
                Py_INCREF(key);
                key = make_utf8_string(key);
 
                Py_INCREF(value);
                value = make_utf8_string(value);
                
                item = prefix_new();
                if (!item) {
                    Py_DECREF(key);
                    Py_DECREF(value);
                    
                    PyErr_SetString(PyExc_RuntimeError, "out of memory");
                    return -1;
                }

                item->uri = strdup(PyString_AS_STRING(key));
                item->prefix = strdup(PyString_AS_STRING(value));
                
                if (prefixes)
                    prefix_append(prefixes, item);
                else
                    prefixes = item;
                
                Py_DECREF(key);
                Py_DECREF(value);
            }
        }
    }

    *list = prefixes;
    return 0;
}

static int do_serialize(PyObject *element,
                        char *defaultNS, prefix_t *prefixes,
                        int closeElement, int *prefixCounter,
                        char *buf, int pos, int len)
{
    PyObject *o;
    char *name, *s;
    int size, total, namesize, i, ret, keysize, prefixsize, valsize, ok;
    PyObject *elemname, *attrs, *key, *value, *children, *child, *uri, *defUri,
        *class, *clsname;
    PyObject *keyns, *keyname, *keyval, *localPrefs;
    prefix_t *lprefixes, *found, *next;
    Py_ssize_t dictpos = 0;
    int uri_size = 0, defUri_size = 0;
    prefix_t *prefix = NULL;
    prefix_t *nameprefix = NULL;
    prefix_t *attrprefix = NULL;
    char *defUri_s = NULL;
    char *uri_s = NULL;

    ret = 0;
    uri = NULL;
    attrs = NULL;
    elemname = NULL;
    defUri = NULL;
    uri = NULL;
    children = NULL;

    /* handle content */
    if (PyString_Check(element) || PyUnicode_Check(element)) {
        if (PyUnicode_Check(element)) {
            o = PyUnicode_AsUTF8String(element);
        } else {
            o = element;
            Py_INCREF(o);
        }

        s = PyString_AS_STRING(o);
        size = PyString_GET_SIZE(o);

        class = PyObject_GetAttrString(element, "__class__");
        clsname = PyObject_GetAttrString(class, "__name__");
        if (strcmp("SerializedXML", PyString_AS_STRING(clsname)) == 0) {
            strcpy(&buf[pos], s);
            pos += size;
        } else {
            pos = encode(s, size, 0, buf, pos, len);
        }
        Py_DECREF(clsname);
        Py_DECREF(class);

        Py_DECREF(o);

        if (!pos) goto error;

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

    defUri = PyObject_GetAttrString(element, "defaultUri");
    if (defUri != Py_None && !PyString_Check(defUri) &&
        !PyUnicode_Check(defUri)) {
        ret = -1;
        goto error;
    }

    if (PyString_Check(defUri) || PyUnicode_Check(defUri)) {
        defUri = make_utf8_string(defUri);
        defUri_s = PyString_AS_STRING(defUri);
        defUri_size = PyString_GET_SIZE(defUri);
    } else {
        defUri_s = defaultNS;
        if (defUri_s)
            defUri_size = strlen(defUri_s);
    }

    /* prefixes */

    /* handle localPrefixes */
    if (PyObject_HasAttrString(element, "localPrefixes")) {
        localPrefs = PyObject_GetAttrString(element, "localPrefixes");
        ok = convert_from_dict(localPrefs, &lprefixes);
        Py_DECREF(localPrefs);
        localPrefs = NULL;
        if (ok < 0) {
            ret = -1;
            goto error;
        }

        prefix = lprefixes;
        while (prefix) {
            next = prefix->next;
            for (found = prefixes; found; found = found->next) {
                if (strcmp(found->prefix, prefix->prefix) == 0)
                    break;
            }

            if (!found) {
                prefix_append(prefixes, prefix);
            } else {
                prefix_free(prefix);
            }

            prefix = next;
        }
    }

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
        uri = make_utf8_string(uri);
        uri_s = PyString_AS_STRING(uri);
        uri_size = PyString_GET_SIZE(uri);

        for (prefix = prefixes; prefix; prefix = prefix->next) {
            if (strcmp(prefix->uri, uri_s) == 0)
                break;
        }

        if (defUri_s && strcmp(uri_s, defUri_s) != 0) {
            prefix = prefix_find_uri(&prefixes, uri_s, prefixCounter);
            if (!prefix->in_scope) {
                prefix->needs_write = 1;
                prefix->in_scope = 1;
            }
            nameprefix = prefix;
        } else if (prefix) {
            if (!prefix->in_scope) {
                prefix->needs_write = 1;
                prefix->in_scope = 1;
            }
            nameprefix = prefix;
        }
    } else {
        Py_DECREF(uri);
        uri = NULL;

        uri_s = defaultNS;
        if (uri_s)
            uri_size = strlen(defaultNS);
    }

    if (!PyObject_HasAttrString(element, "name")) {
        ret = -1;
        goto error;
    }

    elemname = PyObject_GetAttrString(element, "name");
    if (PyString_Check(elemname) || PyUnicode_Check(elemname)) {
        elemname = make_utf8_string(elemname);
    } else {
        ret = -1;
        goto error;
    }
    
    name = PyString_AS_STRING(elemname);
    namesize = PyString_GET_SIZE(elemname);

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

    dictpos = 0;
    while (PyDict_Next(attrs, &dictpos, &key, &value)) {
        attrprefix = NULL;
        keyname = NULL;
        keyval = NULL;
        keyns = NULL;

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

            /* turn borrowed refs into new refs */
            Py_INCREF(keyns);
            Py_INCREF(keyname);
            Py_INCREF(value);
            keyns = make_utf8_string(keyns);
            keyname = make_utf8_string(keyname);
            value = make_utf8_string(value);

            attrprefix = prefix_find_uri(&prefixes, PyString_AS_STRING(keyns),
                                         prefixCounter);
            if (!attrprefix->in_scope) attrprefix->needs_write = 1;

            if (keyns) {
                Py_DECREF(keyns);
                keyns = NULL;
            }
        } else {
            keyname = key;
            /* turn borrowed refs into new refs */
            Py_INCREF(keyname);
            Py_INCREF(value);
            keyname = make_utf8_string(keyname);
            value = make_utf8_string(value);
        }

        keysize = PyString_GET_SIZE(keyname);
        valsize = PyString_GET_SIZE(value);
        if (!attrprefix) {
            total = keysize + valsize;
            if (total + 4 > (len - pos)) {
                if (keyname) { Py_DECREF(keyname); }
                if (value) { Py_DECREF(value); }
                goto error;
            }

            buf[pos++] = ' ';
            strcpy(&buf[pos], PyString_AS_STRING(keyname));
            pos += keysize;
        } else {
            prefixsize = strlen(attrprefix->prefix);
            total = keysize + valsize + prefixsize;
            if ((total + 5) > (len - pos)) {
                if (keyname) { Py_DECREF(keyname); }
                if (value) { Py_DECREF(value); }
                goto error;
            }
            buf[pos++] = ' ';
            strcpy(&buf[pos], attrprefix->prefix);
            pos += prefixsize;
            buf[pos++] = ':';
            strcpy(&buf[pos], PyString_AS_STRING(keyname));
            pos += keysize;
        }
        buf[pos++] = '=';
        buf[pos++] = '\'';
        pos = encode(PyString_AS_STRING(value), valsize, 1,
                     buf, pos, len);
        if (!pos) {
            if (keyname) { Py_DECREF(keyname); }
            if (value) { Py_DECREF(value); }
            goto error;
        }
        buf[pos++] = '\'';

        if (keyname) { Py_DECREF(keyname); }
        if (value) { Py_DECREF(value); }
    }


    /* write out namespaces and prefixes */
    if (((defaultNS && defUri_s && strcmp(defaultNS, defUri_s) != 0) ||
         (!defaultNS && defUri_s)) &&
        uri_s && (strcmp(uri_s, defUri_s) != 0 ||
                  !nameprefix || !nameprefix->in_scope)) {
        if ((defUri_size + 9) > (len - pos))
            goto error;
        strcpy(&buf[pos], " xmlns='");
        pos += 8;
        strcpy(&buf[pos], defUri_s);
        pos += defUri_size;
        buf[pos++] = '\'';
    }

    for (prefix = prefixes; prefix; prefix = prefix->next) {
        if (prefix->needs_write && strcmp(prefix->prefix, "xml") != 0) {
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

    /* push prefixes onto the stack */
    for (prefix = prefixes; prefix; prefix = prefix->next)
        prefix->stack_height++;

    size = PyList_GET_SIZE(children);
    if (size > 0) {
        if (1 > (len - pos))
            goto error;
        buf[pos++] = '>';

        for (i = 0; i < size; i++) {
            child = PyList_GET_ITEM(children, i);
            pos = do_serialize(child, defUri_s, prefixes,
                               closeElement, prefixCounter,
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

    /* pop the prefix stack */
    for (prefix = prefixes; prefix; prefix = prefix->next) {
        prefix->stack_height--;
        if (prefix->stack_height == 0)
            prefix->in_scope = 0;
    }

    ret =  pos;
    /* fall through */

error:
    if (children) { Py_DECREF(children); }
    if (attrs) { Py_DECREF(attrs); }
    if (elemname) { Py_DECREF(elemname); }
    if (uri) { Py_DECREF(uri); }
    if (defUri) { Py_DECREF(defUri); }

    return ret;
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
    int prefixCounter = 0;
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
    prefixes->stack_height = 1;

    /* convert prefix dict to internal list structure */
    plist = NULL;
    ok = convert_from_dict(prefixdict, &plist);
    if (ok < 0) return NULL;
    prefixes->next = plist;

    if (prefixesInScope) {
        if (prefixesInScope != Py_None && !PyList_Check(prefixesInScope)) {
            PyErr_SetString(PyExc_TypeError,
                            "Expected list or none for prefixesInScope.");
            if (prefixes) prefix_free_list(prefixes);
            return NULL;
        }
        if (PyList_Check(prefixesInScope)) {
            for (i = 0; i < PyList_GET_SIZE(prefixesInScope); i++) {
                value = PyList_GET_ITEM(prefixesInScope, i);
                if (!PyString_Check(value) && !PyUnicode_Check(value)) {
                    PyErr_SetString(PyExc_TypeError,
                                    "Expected strings in prefixesInScope.");
                    if (prefixes) prefix_free_list(prefixes);
                    return NULL;
                }

                Py_INCREF(value);
                value = make_utf8_string(value);

                for (found = prefixes; found; found = found->next)
                    if (strcmp(PyString_AS_STRING(value), found->prefix) == 0)
                        break;
                
                if (found)
                    found->in_scope = 1;
                
                Py_DECREF(value);
            }
        }
    }

    dynbuf = NULL;
    size = do_serialize(element, NULL, prefixes, closeElement, 
                        &prefixCounter, buf, 0, len);
    while (size == 0) {
        if (dynbuf)
            free(dynbuf);
        len *= 2;
        dynbuf = (char *)malloc(len);
        if (!dynbuf) {
            if (prefixes) prefix_free_list(prefixes);
            Py_RETURN_NONE;
        }
        size = do_serialize(element, NULL, prefixes, closeElement,
                            &prefixCounter, 
                            dynbuf, 0, len);
    }

    if (prefixes) prefix_free_list(prefixes);

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
