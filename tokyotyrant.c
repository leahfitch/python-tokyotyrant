#include <Python.h>
#include <tcrdb.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>


static PyObject *
tcmap2pydict(TCMAP *map)
{
    const char *kstr, *vstr;
    PyObject *dict, *value;
    
    dict = PyDict_New();
    
    if (dict == NULL)
    {
        PyErr_SetString(PyExc_MemoryError, "Could not allocate memory for dict.");
        return NULL;
    }
    
    tcmapiterinit(map);
    kstr = tcmapiternext2(map);
    
    while (kstr != NULL)
    {
        vstr = tcmapget2(map, kstr);
        
        if (vstr == NULL)
        {
            Py_DECREF(dict);
            PyErr_SetString(PyExc_MemoryError, "Could not allocate memory for map value.");
            return NULL;
        }
        
        value = PyString_FromString(vstr);
        
        if (value == NULL)
        {
            Py_DECREF(dict);
            PyErr_SetString(PyExc_MemoryError, "Could not allocate memory for dict value.");
            return NULL;
        }
        
        if (PyDict_SetItemString(dict, kstr, value) != 0)
        {
            Py_DECREF(value);
            Py_DECREF(dict);
            PyErr_SetString(PyExc_Exception, "Could not set dict item.");
            return NULL;
        }
        
        Py_DECREF(value);
        
        kstr = tcmapiternext2(map);
    }
    
    return dict;
}


static TCMAP *
pydict2tcmap(PyObject *dict)
{
    if (!PyDict_Check(dict))
    {
        PyErr_SetString(PyExc_TypeError, "Argument is not a dict.");
        return NULL;
    }
    
    PyObject *key, *value;
    int pos = 0;
    const char *kstr, *vstr;
    TCMAP *map;
    
    map = tcmapnew();
    
    if (map == NULL)
    {
        PyErr_SetString(PyExc_MemoryError, "Could not allocate map.");
        return NULL;
    }
    
    while (PyDict_Next(dict, &pos, &key, &value))
    {
        if (!PyString_Check(value))
        {
            tcmapdel(map);
            PyErr_SetString(PyExc_TypeError, "All values must be strings.");
            return NULL;
        }
        
        kstr = PyString_AsString(key);
        vstr = PyString_AsString(value);
        
        tcmapput2(map, kstr, vstr);
    }
    
    return map;
}


static PyObject *TyrantError;


static void
raise_tyrant_error(TCRDB *db)
{
    int code = tcrdbecode(db);
    const char *msg = tcrdberrmsg(code);
    
    if (code == TCENOREC)
    {
        PyErr_SetString(PyExc_KeyError, msg);
    }
    else
    {
        PyErr_SetString(TyrantError, msg);
    }
}


static PyTypeObject TyrantType;
static PyTypeObject TyrantQueryType;


typedef struct
{
    PyObject_HEAD
    TCRDB *db;
} Tyrant;


typedef struct
{
    PyObject_HEAD
    RDBQRY *q;
} TyrantQuery;


static long
TyrantQuery_Hash(PyObject *self)
{
    PyErr_SetString(PyExc_TypeError, "Tyrant query objects are not hashable.");
    return -1;
}


static void
TyrantQuery_dealloc(TyrantQuery *self)
{
    if (self->q)
    {
        Py_BEGIN_ALLOW_THREADS
        tcrdbqrydel(self->q);
        Py_END_ALLOW_THREADS
    }
    self->ob_type->tp_free(self);
}


static PyObject *
TyrantQuery_new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
    TyrantQuery *self;
    Tyrant *pydb;
    
    self = (TyrantQuery *) type->tp_alloc(type, 0);
    if (!self)
    {
        PyErr_SetString(PyExc_MemoryError, "Cannot allocate TyrantQuery instance.");
        return NULL;
    }
    
    if (PyArg_ParseTuple(args, "O!", &TyrantType, &pydb))
    {
        self->q = tcrdbqrynew(pydb->db);
        if (!self->q)
        {
            raise_tyrant_error(pydb->db);
        }
        else
        {
            return (PyObject *) self;
        }
    }
    
    TyrantQuery_dealloc(self);
    return NULL;
}


static PyObject *
TyrantQuery_addcond(TyrantQuery *self, PyObject *args, PyObject *kwargs)
{
    const char *name, *expr;
    name = expr = NULL;
    int op = 0;
    
    static char *kwlist[] = {"name", "op", "expr", NULL};
    
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "si|s:addcond", kwlist, 
        &name, &op, &expr))
    {
        return NULL;
    }
    
    Py_BEGIN_ALLOW_THREADS
    tcrdbqryaddcond(self->q, name, op, expr);
    Py_END_ALLOW_THREADS
    
    Py_RETURN_NONE;
}


static PyObject *
TyrantQuery_setorder(TyrantQuery *self, PyObject *args)
{
    const char *name;
    int type;
    
    if (!PyArg_ParseTuple(args, "si:setorder", &name, &type))
    {
        return NULL;
    }
    
    Py_BEGIN_ALLOW_THREADS
    tcrdbqrysetorder(self->q, name, type);
    Py_END_ALLOW_THREADS
    
    Py_RETURN_NONE;
}


static PyObject *
TyrantQuery_setlimit(TyrantQuery *self, PyObject *args)
{
    int max, skip;
    max = skip = -1;
    
    if (!PyArg_ParseTuple(args, "ii:setlimit", &max, &skip))
    {
        return NULL;
    }
    
    Py_BEGIN_ALLOW_THREADS
    tcrdbqrysetlimit(self->q, max, skip);
    Py_END_ALLOW_THREADS
    
    Py_RETURN_NONE;
}


static PyObject *
TyrantQuery_search(TyrantQuery *self)
{
    TCLIST *results;
    int n = 0, i=0, vsiz = 0;
    const char *vbuf;
    PyObject *pylist, *val;
    
    Py_BEGIN_ALLOW_THREADS
    results = tcrdbqrysearch(self->q);
    Py_END_ALLOW_THREADS
    
    if (!results)
    {
        PyErr_SetString(PyExc_MemoryError, "Cannot allocate memory for TCLIST object");
        return NULL;
    }
    
    n = tclistnum(results);
    pylist = PyList_New(n);
    
    if (pylist)
    {
        for (i=0; i<n; i++)
        {
            vbuf = tclistval(results, i, &vsiz);
            val = PyString_FromStringAndSize(vbuf, vsiz);
            PyList_SET_ITEM(pylist, i, val);
            
            vsiz = 0;
        }
    }
    tclistdel(results);
    
    return pylist;
}


static PyObject *
TyrantQuery_searchout(TyrantQuery *self)
{
    bool success;
    
    Py_BEGIN_ALLOW_THREADS
    success = tcrdbqrysearchout(self->q);
    Py_END_ALLOW_THREADS
    
    return Py_BuildValue("i", success);
}


static PyObject *
TyrantQuery_searchget(TyrantQuery *self)
{
    TCLIST *results;
    int n=0, i=0;
    TCMAP *map;
    PyObject *pylist, *dict;
    
    Py_BEGIN_ALLOW_THREADS
    results = tcrdbqrysearchget(self->q);
    Py_END_ALLOW_THREADS
    
    if (!results)
    {
        PyErr_SetString(PyExc_MemoryError, "Cannot allocate memory for TCLIST object");
        return NULL;
    }
    
    n = tclistnum(results);
    pylist = PyList_New(n);
    
    if (pylist)
    {
        for (i=0; i<n; i++)
        {
            map = tcrdbqryrescols(results, i);
            
            if (!map)
            {
                PyErr_SetString(PyExc_MemoryError, "Cannot allocate memory for TCMAP object");
                tclistdel(results);
                return NULL;
            }
            
            dict = tcmap2pydict(map);
            
            if (!dict)
            {
                tcmapdel(map);
                tclistdel(results);
                return NULL;
            }
            
            PyList_SET_ITEM(pylist, i, dict);
            
            tcmapdel(map);
        }
    }
    tclistdel(results);
    
    return pylist;
}


static PyObject *
TyrantQuery_searchcount(TyrantQuery *self)
{
    int n = 0;
    
    Py_BEGIN_ALLOW_THREADS
    n = tcrdbqrysearchcount(self->q);
    Py_END_ALLOW_THREADS
    
    return Py_BuildValue("i", n);
}


static PyObject *
TyrantQuery_hint(TyrantQuery *self)
{
    const char *hint;
    
    Py_BEGIN_ALLOW_THREADS
    hint = tcrdbqryhint(self->q);
    Py_END_ALLOW_THREADS
    
    return PyString_FromString(hint);
}


static PyMethodDef TyrantQuery_methods[] = 
{
    {
        "addcond", (PyCFunction) TyrantQuery_addcond,
        METH_VARARGS | METH_KEYWORDS,
        "Add a condition."
    },
    
    {
        "setorder", (PyCFunction) TyrantQuery_setorder,
        METH_VARARGS,
        "Set the column and direction to order by."
    },
    
    {
        "setlimit", (PyCFunction) TyrantQuery_setlimit,
        METH_VARARGS,
        "Set the offset and limit of the results."
    },
    
    {
        "search", (PyCFunction) TyrantQuery_search,
        METH_NOARGS,
        "Run the query. Returns the keys of matching records"
    },
    
    {
        "searchout", (PyCFunction) TyrantQuery_searchout,
        METH_NOARGS,
        "Remove all matching records."
    },
    
    {
        "searchget", (PyCFunction) TyrantQuery_searchget,
        METH_NOARGS,
        "Run the query. Returns the matching records."
    },
    
    {
        "searchcount", (PyCFunction) TyrantQuery_searchcount,
        METH_NOARGS,
        "Get a count of matching records."
    },
    
    {
        "hint", (PyCFunction) TyrantQuery_hint,
        METH_NOARGS,
        "Get the hint for a query."
    },
    
    { NULL }
};


static PyTypeObject TyrantQueryType = {
  PyObject_HEAD_INIT(NULL)
  0,                                           /* ob_size */
  "tokyocabinet.tyrant.TyrantQuery",           /* tp_name */
  sizeof(TyrantQuery),                         /* tp_basicsize */
  0,                                           /* tp_itemsize */
  (destructor)TyrantQuery_dealloc,             /* tp_dealloc */
  0,                                           /* tp_print */
  0,                                           /* tp_getattr */
  0,                                           /* tp_setattr */
  0,                                           /* tp_compare */
  0,                                           /* tp_repr */
  0,                                           /* tp_as_number */
  0,                                           /* tp_as_sequence */
  0,                                           /* tp_as_mapping */
  TyrantQuery_Hash,                            /* tp_hash  */
  0,                                           /* tp_call */
  0,                                           /* tp_str */
  0,                                           /* tp_getattro */
  0,                                           /* tp_setattro */
  0,                                           /* tp_as_buffer */
  Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,    /* tp_flags */
  "Tyrant database query",                     /* tp_doc */
  0,                                           /* tp_traverse */
  0,                                           /* tp_clear */
  0,                                           /* tp_richcompare */
  0,                                           /* tp_weaklistoffset */
  0,                                           /* tp_iter */
  0,                                           /* tp_iternext */
  TyrantQuery_methods,                         /* tp_methods */
  0,                                           /* tp_members */
  0,                                           /* tp_getset */
  0,                                           /* tp_base */
  0,                                           /* tp_dict */
  0,                                           /* tp_descr_get */
  0,                                           /* tp_descr_set */
  0,                                           /* tp_dictoffset */
  0,                                           /* tp_init */
  0,                                           /* tp_alloc */
  TyrantQuery_new,                             /* tp_new */
};


static long
Tyrant_Hash(PyObject *self)
{
    PyErr_SetString(PyExc_TypeError, "Tyrant objects are not hashable.");
    return -1;
}


static void
Tyrant_dealloc(Tyrant *self)
{
    if (self->db)
    {
        Py_BEGIN_ALLOW_THREADS
        tcrdbdel(self->db);
        Py_END_ALLOW_THREADS
    }
    self->ob_type->tp_free(self);
}


static PyObject *
Tyrant_new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
    Tyrant *self;
    
    self = (Tyrant *) type->tp_alloc(type, 0);
    if (!self)
    {
        PyErr_SetString(PyExc_MemoryError, "Cannot allocate Tyrant instance.");
        return NULL;
    }
    
    self->db = tcrdbnew();
    if (!self->db)
    {
        PyErr_SetString(PyExc_MemoryError, "Cannot allocate TCRDB instance.");
        return NULL;
    }
    
    return (PyObject *) self;
}


static PyObject *
Tyrant_tune(Tyrant *self, PyObject *args, PyObject *kwargs)
{
    bool success = 0;
    double timeout;
    int opts = 0;
    
    static char *kwlist[] = {"timeout", "opts", NULL };
    
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "di:tune", kwlist, 
        &timeout, &opts))
    {
        return NULL;
    }
    
    Py_BEGIN_ALLOW_THREADS
    success = tcrdbtune(self->db, timeout, opts);
    Py_END_ALLOW_THREADS
    
    if (!success)
    {
        raise_tyrant_error(self->db);
        return NULL;
    }
    Py_RETURN_NONE;
}


static PyObject *
Tyrant_open(Tyrant *self, PyObject *args, PyObject *kwargs)
{
    char *host = NULL;
    int port = 0;
    static char *kwlist[] = { "host", "port", NULL };
    
    if (PyArg_ParseTupleAndKeywords(args, kwargs, "si:open", kwlist, &host, &port))
    {
        bool success = 0;
        Py_BEGIN_ALLOW_THREADS
        success = tcrdbopen(self->db, host, port);
        Py_END_ALLOW_THREADS
        if (success)
        {
            Py_RETURN_NONE;
        }
        raise_tyrant_error(self->db);
    }
    return NULL;
}


static PyObject *
Tyrant_close(Tyrant *self)
{
    bool success = 0;
    Py_BEGIN_ALLOW_THREADS
    success = tcrdbclose(self->db);
    Py_END_ALLOW_THREADS
    if (!success)
    {
        raise_tyrant_error(self->db);
        return NULL;
    }
    Py_RETURN_NONE;
}


static PyObject *
Tyrant_put(Tyrant *self, PyObject *args)
{
    bool success;
    char *kbuf, *vbuf;
    int ksiz, vsiz;
    
    if (!PyArg_ParseTuple(args, "s#s#:put", &kbuf, &ksiz, &vbuf, &vsiz))
    {
        return NULL;
    }
    
    Py_BEGIN_ALLOW_THREADS
    success = tcrdbput(self->db, kbuf, ksiz, vbuf, vsiz);
    Py_END_ALLOW_THREADS
    
    if (!success)
    {
        raise_tyrant_error(self->db);
        return NULL;
    }
    Py_RETURN_NONE;
}


static PyObject *
Tyrant_putkeep(Tyrant *self, PyObject *args)
{
    bool success;
    char *kbuf, *vbuf;
    int ksiz, vsiz;
    
    if (!PyArg_ParseTuple(args, "s#s#:putkeep", &kbuf, &ksiz, &vbuf, &vsiz))
    {
        return NULL;
    }
    
    Py_BEGIN_ALLOW_THREADS
    success = tcrdbputkeep(self->db, kbuf, ksiz, vbuf, vsiz);
    Py_END_ALLOW_THREADS
    
    if (!success)
    {
        raise_tyrant_error(self->db);
        return NULL;
    }
    Py_RETURN_NONE;
}


static PyObject *
Tyrant_putcat(Tyrant *self, PyObject *args)
{
    bool success;
    char *kbuf, *vbuf;
    int ksiz, vsiz;
    
    if (!PyArg_ParseTuple(args, "s#s#:putcat", &kbuf, &ksiz, &vbuf, &vsiz))
    {
        return NULL;
    }
    
    Py_BEGIN_ALLOW_THREADS
    success = tcrdbputcat(self->db, kbuf, ksiz, vbuf, vsiz);
    Py_END_ALLOW_THREADS
    
    if (!success)
    {
        raise_tyrant_error(self->db);
        return NULL;
    }
    Py_RETURN_NONE;
}


static PyObject *
Tyrant_putnr(Tyrant *self, PyObject *args)
{
    bool success;
    char *kbuf, *vbuf;
    int ksiz, vsiz;
    
    if (!PyArg_ParseTuple(args, "s#s#:putnr", &kbuf, &ksiz, &vbuf, &vsiz))
    {
        return NULL;
    }
    
    Py_BEGIN_ALLOW_THREADS
    success = tcrdbputnr(self->db, kbuf, ksiz, vbuf, vsiz);
    Py_END_ALLOW_THREADS
    
    if (!success)
    {
        raise_tyrant_error(self->db);
        return NULL;
    }
    Py_RETURN_NONE;
}


static PyObject *
Tyrant_out(Tyrant *self, PyObject *args)
{
    bool success;
    char *kbuf;
    int ksiz;
    
    if (!PyArg_ParseTuple(args, "s#:out", &kbuf, &ksiz))
    {
        return NULL;
    }
    
    Py_BEGIN_ALLOW_THREADS
    success = tcrdbout(self->db, kbuf, ksiz);
    Py_END_ALLOW_THREADS
    
    if (!success)
    {
        raise_tyrant_error(self->db);
        return NULL;
    }
    Py_RETURN_NONE;
}


static PyObject *
Tyrant_get(Tyrant *self, PyObject *args, PyObject *kwargs)
{
    char *kbuf, *vbuf;
    int ksiz, vsiz;
    PyObject *default_value = NULL;
    PyObject *value = NULL;
    
    static char *kwlist[] = {"key", "default", NULL};
    
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s#|O:outdup", kwlist, &kbuf, &ksiz, &default_value))
    {
        return NULL;
    }
    
    Py_BEGIN_ALLOW_THREADS
    vbuf = tcrdbget(self->db, kbuf, ksiz, &vsiz);
    Py_END_ALLOW_THREADS
    
    if (!vbuf)
    {
        if (default_value)
        {
            return default_value;
        }
        Py_RETURN_NONE;
    }
    
    value = PyString_FromStringAndSize(vbuf, vsiz);
    free(vbuf);
    
    if (!value)
    {
        return NULL;
    }
    
    return value;
}


static PyObject *
Tyrant_vsiz(Tyrant *self, PyObject *args)
{
    char *kbuf;
    int ksiz, vsiz;
    
    if (!PyArg_ParseTuple(args, "s#:vsiz", &kbuf, &ksiz))
    {
        return NULL;
    }
    
    Py_BEGIN_ALLOW_THREADS
    vsiz = tcrdbvsiz(self->db, kbuf, ksiz);
    Py_END_ALLOW_THREADS
    
    return Py_BuildValue("i", vsiz);
}


static PyObject *
Tyrant_fwmkeys(Tyrant *self, PyObject *args, PyObject *kwargs)
{
    char *pbuf;
    int psiz, i, n;
    int max = -1;
    PyObject *pylist;
    TCLIST *list;
    
    static char *kwlist[] = {"prefix", "max", NULL};
    
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s#|i:fwmkeys", kwlist,
        &pbuf, &psiz, &max))
    {
        return NULL;
    }
    
    Py_BEGIN_ALLOW_THREADS
    list = tcrdbfwmkeys(self->db, pbuf, psiz, max);
    Py_END_ALLOW_THREADS
    
    if (!list)
    {
        PyErr_SetString(PyExc_MemoryError, "Cannot allocate memort for TCLIST object");
        return NULL;
    }
    
    n = tclistnum(list);
    pylist = PyList_New(n);
    if (pylist)
    {
        for (i=0; i<n; i++)
        {
            int vsiz;
            const char *vbuf;
            PyObject *value;
            
            vbuf = tclistval(list, i, &vsiz);
            value = PyString_FromStringAndSize(vbuf, vsiz);
            PyList_SET_ITEM(pylist, i, value);
        }
    }
    tclistdel(list);
    
    return pylist;
}


static PyObject *
Tyrant_addint(Tyrant *self, PyObject *args)
{
    char *kbuf;
    int ksiz, num, result;
    
    if (!PyArg_ParseTuple(args, "s#i:addint", &kbuf, &ksiz, &num))
    {
        return NULL;
    }
    
    Py_BEGIN_ALLOW_THREADS
    result = tcrdbaddint(self->db, kbuf, ksiz, num);
    Py_END_ALLOW_THREADS
    
    return PyInt_FromLong((long) result);
}


static PyObject *
Tyrant_adddouble(Tyrant *self, PyObject *args)
{
    char *kbuf;
    int ksiz;
    double num, result;
    
    if (!PyArg_ParseTuple(args, "s#d:adddouble", &kbuf, &ksiz, &num))
    {
        return NULL;
    }
    
    Py_BEGIN_ALLOW_THREADS
    result = tcrdbadddouble(self->db, kbuf, ksiz, num);
    Py_END_ALLOW_THREADS
    
    return PyFloat_FromDouble(result);
}


static PyObject *
Tyrant_sync(Tyrant *self)
{
    bool success;
    
    Py_BEGIN_ALLOW_THREADS
    success = tcrdbsync(self->db);
    Py_END_ALLOW_THREADS
    
    if (!success)
    {
        raise_tyrant_error(self->db);
        return NULL;
    }
    
    Py_RETURN_NONE;
}


static PyObject *
Tyrant_optimize(Tyrant *self, PyObject *args, PyObject *kwargs)
{
    bool success = 0;
    char *params = NULL;
    
    static char *kwlist[] = {"params", NULL };
    
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s:optimize", kwlist, 
        &params))
    {
        return NULL;
    }
    
    Py_BEGIN_ALLOW_THREADS
    success = tcrdboptimize(self->db, params);
    Py_END_ALLOW_THREADS
    
    if (!success)
    {
        raise_tyrant_error(self->db);
        return NULL;
    }
    Py_RETURN_NONE;
}


static PyObject *
Tyrant_vanish(Tyrant *self)
{
    bool success;
    
    Py_BEGIN_ALLOW_THREADS
    success = tcrdbvanish(self->db);
    Py_END_ALLOW_THREADS
    
    if (!success)
    {
        raise_tyrant_error(self->db);
        return NULL;
    }
    
    Py_RETURN_NONE;
}


static PyObject *
Tyrant_copy(Tyrant *self, PyObject *args)
{
    bool success;
    char *path;
    
    if (!PyArg_ParseTuple(args, "s", &path))
    {
        return NULL;
    }
    
    Py_BEGIN_ALLOW_THREADS
    success = tcrdbcopy(self->db, path);
    Py_END_ALLOW_THREADS
    
    if (!success)
    {
        raise_tyrant_error(self->db);
        return NULL;
    }
    
    Py_RETURN_NONE;
}


static PyObject *
Tyrant_restore(Tyrant *self, PyObject *args, PyObject *kwargs)
{
    bool success = 0;
    char *path = NULL;
    uint64_t ts = 0;
    int opts = 0;
    
    static char *kwlist[] = {"path", "ts", "opts", NULL };
    
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "sK|i:restore", kwlist, 
        &path, &ts, &opts))
    {
        return NULL;
    }
    
    Py_BEGIN_ALLOW_THREADS
    success = tcrdbrestore(self->db, path, ts, opts);
    Py_END_ALLOW_THREADS
    
    if (!success)
    {
        raise_tyrant_error(self->db);
        return NULL;
    }
    Py_RETURN_NONE;
}


static PyObject *
Tyrant_setmst(Tyrant *self, PyObject *args, PyObject *kwargs)
{
    bool success = 0;
    char *host = NULL;
    int port = 0;
    uint64_t ts = 0;
    int opts = 0;
    
    static char *kwlist[] = {"host", "port", "ts", "opts", NULL };
    
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "siK|i:restore", kwlist, 
        &host, &port, &ts, &opts))
    {
        return NULL;
    }
    
    Py_BEGIN_ALLOW_THREADS
    success = tcrdbsetmst(self->db, host, port, ts, opts);
    Py_END_ALLOW_THREADS
    
    if (!success)
    {
        raise_tyrant_error(self->db);
        return NULL;
    }
    Py_RETURN_NONE;
}


static PyObject *
Tyrant_rnum(Tyrant *self)
{
    uint64_t rnum;
    
    Py_BEGIN_ALLOW_THREADS
    rnum = tcrdbrnum(self->db);
    Py_END_ALLOW_THREADS
    
    return PyLong_FromLongLong(rnum);
}


static PyObject *
Tyrant_size(Tyrant *self)
{
    uint64_t fsiz;
    
    Py_BEGIN_ALLOW_THREADS
    fsiz = tcrdbsize(self->db);
    Py_END_ALLOW_THREADS
    
    return PyLong_FromLongLong(fsiz);
}


static PyObject *
Tyrant_stat(Tyrant *self)
{
    char *stat;
    
    Py_BEGIN_ALLOW_THREADS
    stat = tcrdbstat(self->db);
    Py_END_ALLOW_THREADS
    
    return PyString_FromString(stat);
}


static PyObject *
Tyrant_tblput(Tyrant *self, PyObject *args)
{
    bool success;
    char *kbuf;
    int ksiz;
    TCMAP *cols;
    PyObject *dict;
    
    if (!PyArg_ParseTuple(args, "s#O:put", &kbuf, &ksiz, &dict))
    {
        return NULL;
    }
    
    cols = pydict2tcmap(dict);
    
    if (cols == NULL)
    {
        return NULL;
    }
    
    Py_BEGIN_ALLOW_THREADS
    success = tcrdbtblput(self->db, kbuf, ksiz, cols);
    Py_END_ALLOW_THREADS
    
    tcmapdel(cols);
    
    if (!success)
    {
        raise_tyrant_error(self->db);
        return NULL;
    }
    
    Py_RETURN_NONE;
}


static PyObject *
Tyrant_tblputkeep(Tyrant *self, PyObject *args)
{
    bool success;
    char *kbuf;
    int ksiz;
    TCMAP *cols;
    PyObject *dict;
    
    if (!PyArg_ParseTuple(args, "s#O:put", &kbuf, &ksiz, &dict))
    {
        return NULL;
    }
    
    cols = pydict2tcmap(dict);
    
    if (cols == NULL)
    {
        return NULL;
    }
    
    Py_BEGIN_ALLOW_THREADS
    success = tcrdbtblputkeep(self->db, kbuf, ksiz, cols);
    Py_END_ALLOW_THREADS
    
    tcmapdel(cols);
    
    if (!success)
    {
        raise_tyrant_error(self->db);
        return NULL;
    }
    Py_RETURN_NONE;
}


static PyObject *
Tyrant_tblputcat(Tyrant *self, PyObject *args)
{
    bool success;
    char *kbuf;
    int ksiz;
    TCMAP *cols;
    PyObject *dict;
    
    if (!PyArg_ParseTuple(args, "s#O:put", &kbuf, &ksiz, &dict))
    {
        return NULL;
    }
    
    cols = pydict2tcmap(dict);
    
    if (cols == NULL)
    {
        return NULL;
    }
    
    Py_BEGIN_ALLOW_THREADS
    success = tcrdbtblputcat(self->db, kbuf, ksiz, cols);
    Py_END_ALLOW_THREADS
    
    tcmapdel(cols);
    
    if (!success)
    {
        raise_tyrant_error(self->db);
        return NULL;
    }
    Py_RETURN_NONE;
}


static PyObject *
Tyrant_tblout(Tyrant *self, PyObject *args)
{
    bool success;
    char *kbuf;
    int ksiz;
    
    if (!PyArg_ParseTuple(args, "s#:out", &kbuf, &ksiz))
    {
        return NULL;
    }
    
    Py_BEGIN_ALLOW_THREADS
    success = tcrdbtblout(self->db, kbuf, ksiz);
    Py_END_ALLOW_THREADS
    
    if (!success)
    {
        raise_tyrant_error(self->db);
        return NULL;
    }
    Py_RETURN_NONE;
}


static PyObject *
Tyrant_tblget(Tyrant *self, PyObject *args)
{
    char *kbuf;
    int ksiz;
    TCMAP *cols;
    PyObject *value;
    
    if (!PyArg_ParseTuple(args, "s#:get", &kbuf, &ksiz))
    {
        return NULL;
    }
    
    Py_BEGIN_ALLOW_THREADS
    cols = tcrdbtblget(self->db, kbuf, ksiz);
    Py_END_ALLOW_THREADS
    
    if (!cols)
    {
        Py_RETURN_NONE;
    }
    
    value = tcmap2pydict(cols);
    tcmapdel(cols);
    
    if (!value)
    {
        return NULL;
    }
    
    return value;
}


static PyObject *
Tyrant_tblsetindex(Tyrant *self, PyObject *args)
{
    bool success = 0;
    const char *name;
    int opts;
    
    if (!PyArg_ParseTuple(args, "si:setindex", &name, &opts))
    {
        return NULL;
    }
    
    Py_BEGIN_ALLOW_THREADS
    success = tcrdbtblsetindex(self->db, name, opts);
    Py_END_ALLOW_THREADS
    
    if (!success)
    {
        raise_tyrant_error(self->db);
        return NULL;
    }
    Py_RETURN_NONE;
}


static PyObject *
Tyrant_tblgenuid(Tyrant *self)
{
    int64_t id = 0;
    
    Py_BEGIN_ALLOW_THREADS
    id = tcrdbtblgenuid(self->db);
    Py_END_ALLOW_THREADS
    
    if (id < 0)
    {
        raise_tyrant_error(self->db);
        return NULL;
    }
    
    return Py_BuildValue("L", id);
}


static PyObject *
Tyrant_tblquery(Tyrant *self)
{
    PyObject *query;
    PyObject *args;
    
    args = Py_BuildValue("(O)", self);
    query = TyrantQuery_new(&TyrantQueryType, args, NULL);
    Py_DECREF(args);
    
    if (!query)
    {
        raise_tyrant_error(self->db);
        return NULL;
    }
    
    return query;
}


static PyObject *
Tyrant_metasearch(Tyrant *self, PyObject *args)
{
    RDBQRY **queries;
    TyrantQuery *query;
    TCLIST *results;
    int n = 0, i=0, vsiz = 0, type = 0;
    const char *vbuf;
    PyObject *pyresults, *val, *pyqueries, *item;
    
    if (!PyArg_ParseTuple(args, "Oi:metasearch", &pyqueries, &type))
    {
        return NULL;
    }
    
    if (!PyList_Check(pyqueries))
    {
        return NULL;
    }
    
    n = PyList_Size(pyqueries);
    
    if (n == 0)
    {
        pyresults = PyList_New(0);
        return pyresults;
    }
    
    queries = (RDBQRY **) malloc(sizeof(RDBQRY *) * n);
    
    if (queries == NULL)
    {
        PyErr_SetString(PyExc_MemoryError, "Could not allocate queries list.");
        return NULL;
    }
    
    for (i=0; i<n; i++)
    {
        item = PyList_GetItem(pyqueries, i);
        
        if (!PyObject_TypeCheck(item, &TyrantQueryType))
        {
            PyErr_SetString(PyExc_TypeError, "Expected a list of tyrant query objects.");
            free(queries);
            return NULL;
        }
        
        query = (TyrantQuery *) item;
        queries[i] = query->q;
    }
    
    Py_BEGIN_ALLOW_THREADS
    results = tcrdbmetasearch(queries, n, type);
    Py_END_ALLOW_THREADS
    
    free(queries);
    
    if (!results)
    {
        PyErr_SetString(PyExc_MemoryError, "Cannot allocate memory for TCLIST object");
        return NULL;
    }
    
    n = tclistnum(results);
    pyresults = PyList_New(n);
    
    if (pyresults)
    {
        for (i=0; i<n; i++)
        {
            vbuf = tclistval(results, i, &vsiz);
            val = PyString_FromStringAndSize(vbuf, vsiz);
            PyList_SET_ITEM(pyresults, i, val);
            
            vsiz = 0;
        }
    }
    tclistdel(results);
    
    return pyresults;
}


static Py_ssize_t
Tyrant_length(Tyrant *self)
{
    uint64_t rnum;
    
    Py_BEGIN_ALLOW_THREADS
    rnum = tcrdbrnum(self->db);
    Py_END_ALLOW_THREADS
    
    return (Py_ssize_t) rnum;
}


static PyObject *
Tyrant_subscript(Tyrant *self, PyObject *key)
{
    char *kbuf, *vbuf;
    Py_ssize_t ksiz;
    int vsiz;
    PyObject *value;
    
    if (!PyString_Check(key))
    {
        PyErr_SetString(PyExc_ValueError, "Expected key to be a string.");
        return NULL;
    }
    
    PyString_AsStringAndSize(key, &kbuf, &ksiz);
    
    if (!kbuf)
    {
        return NULL;
    }
    
    Py_BEGIN_ALLOW_THREADS
    vbuf = tcrdbget(self->db, kbuf, (int) ksiz, &vsiz);
    Py_END_ALLOW_THREADS
    
    if (!vbuf)
    {
        raise_tyrant_error(self->db);
        return NULL;
    }
    
    value = PyString_FromStringAndSize(vbuf, vsiz);
    free(vbuf);
    
    if (!value)
    {
        return NULL;
    }
    
    return value;
}


static int
Tyrant_ass_subscript(Tyrant *self, PyObject *key, PyObject *value)
{
    bool success;
    char *kbuf, *vbuf;
    Py_ssize_t ksiz, vsiz;
    
    if (!PyString_Check(key))
    {
        PyErr_SetString(PyExc_ValueError, "Expected key to be a string.");
        return -1;
    }
    
    if (!PyString_Check(value))
    {
        PyErr_SetString(PyExc_ValueError, "Expected value to be a string.");
        return -1;
    }
    
    PyString_AsStringAndSize(key, &kbuf, &ksiz);
    if (!kbuf)
    {
        return -1;
    }
    
    PyString_AsStringAndSize(value, &vbuf, &vsiz);
    if (!vbuf)
    {
        return -1;
    }
    
    Py_BEGIN_ALLOW_THREADS
    success = tcrdbput(self->db, kbuf, (int) ksiz, vbuf, (int) vsiz);
    Py_END_ALLOW_THREADS
    
    if (!success)
    {
        raise_tyrant_error(self->db);
        return -1;
    }
    
    return 0;
}


static PyMappingMethods Tyrant_as_mapping = 
{
    (lenfunc) Tyrant_length,
    (binaryfunc) Tyrant_subscript,
    (objobjargproc) Tyrant_ass_subscript
};


static int
Tyrant_contains(Tyrant *self, PyObject *value)
{
    char *kbuf;
    Py_ssize_t ksiz;
    int vsiz;
    
    if (!PyString_Check(value))
    {
        PyErr_SetString(PyExc_ValueError, "Expected value to be a string");
        return -1;
    }
    
    PyString_AsStringAndSize(value, &kbuf, &ksiz);
    if (!kbuf)
    {
        return -1;
    }
    
    Py_BEGIN_ALLOW_THREADS
    vsiz = tcrdbvsiz(self->db, kbuf, (int) ksiz);
    Py_END_ALLOW_THREADS
    
    return vsiz != -1;
}


static PySequenceMethods Tyrant_as_sequence = 
{
    0,                             /* sq_length */
    0,                             /* sq_concat */
    0,                             /* sq_repeat */
    0,                             /* sq_item */
    0,                             /* sq_slice */
    0,                             /* sq_ass_item */
    0,                             /* sq_ass_slice */
    (objobjproc) Tyrant_contains,  /* sq_contains */
    0,                             /* sq_inplace_concat */
    0                              /* sq_inplace_repeat */
};


static PyMethodDef Tyrant_methods[] = 
{
    {
        "tune", (PyCFunction) Tyrant_tune,
        METH_VARARGS | METH_KEYWORDS,
        "Set tuning parameters."
    },
    
    {
        "open", (PyCFunction) Tyrant_open, 
        METH_VARARGS | METH_KEYWORDS,
        "Open the database connection"
    },
    
    {
        "close", (PyCFunction) Tyrant_close,
        METH_NOARGS,
        "Close the database connection"
    },
    
    {
        "put", (PyCFunction) Tyrant_put,
        METH_VARARGS,
        "Store a record. Overwrite existing record."
    },
    
    {
        "putkeep", (PyCFunction) Tyrant_putkeep,
        METH_VARARGS,
        "Store a record. Don't overwrite an existing record."
    },
    
    {
        "putcat", (PyCFunction) Tyrant_putcat,
        METH_VARARGS,
        "Concatenate value on the end of a record. Creates the record if it doesn't exist."
    },
    
    {
        "putnr", (PyCFunction) Tyrant_putnr,
        METH_VARARGS,
        "Store a record. Overwrite existing record. Don't wait for a server response."
    },
    
    {
        "out", (PyCFunction) Tyrant_out,
        METH_VARARGS,
        "Remove a record. If there are duplicates only the first is removed."
    },
    
    {
        "get", (PyCFunction) Tyrant_get,
        METH_VARARGS | METH_KEYWORDS,
        "Retrieve a record. If none is found None or the supplied default value is returned."
    },
    
    {
        "vsiz", (PyCFunction) Tyrant_vsiz,
        METH_VARARGS,
        "Get the size of the of the record for key. If duplicates are found, the first record is used."
    },
    
    {
        "fwmkeys", (PyCFunction) Tyrant_fwmkeys,
        METH_VARARGS | METH_KEYWORDS,
        "Get a list of of keys that match the given prefix."
    },
    
    {
        "addint", (PyCFunction) Tyrant_addint,
        METH_VARARGS,
        "Add an integer to the selected record."
    },
    
    {
        "adddouble", (PyCFunction) Tyrant_adddouble,
        METH_VARARGS,
        "Add a double to the selected record."
    },
    
    {
        "sync", (PyCFunction) Tyrant_sync,
        METH_NOARGS,
        "Sync data with the disk device."
    },
    
    {
        "optimize", (PyCFunction) Tyrant_optimize,
        METH_VARARGS,
        "Optimize a fragmented database."
    },
    
    {
        "vanish", (PyCFunction) Tyrant_vanish,
        METH_NOARGS,
        "Remove all records from the database."
    },
    
    {
        "copy", (PyCFunction) Tyrant_copy,
        METH_VARARGS,
        "Copy the database to a new file."
    },
    
    {
        "restore", (PyCFunction) Tyrant_restore,
        METH_VARARGS | METH_KEYWORDS,
        "Restore the database from the update logs."
    },
    
    {
        "setmst", (PyCFunction) Tyrant_setmst,
        METH_VARARGS | METH_KEYWORDS,
        "Set the replication master."
    },
    
    {
        "rnum", (PyCFunction) Tyrant_rnum,
        METH_NOARGS,
        "Get the number of records in the database."
    },
    
    {
        "size", (PyCFunction) Tyrant_size,
        METH_NOARGS,
        "Get the size of the database in bytes."
    },
    
    {
        "stat", (PyCFunction) Tyrant_stat,
        METH_NOARGS,
        "Get the server status string."
    },
    
    {
        "tblput", (PyCFunction) Tyrant_tblput,
        METH_VARARGS,
        "Store a record. Overwrite existing record."
    },
    
    {
        "tblputkeep", (PyCFunction) Tyrant_tblputkeep,
        METH_VARARGS,
        "Store a record. Don't overwrite an existing record."
    },
    
    {
        "tblputcat", (PyCFunction) Tyrant_tblputcat,
        METH_VARARGS,
        "Concatenate value on the end of a record. Creates the record if it doesn't exist."
    },
    
    {
        "tblout", (PyCFunction) Tyrant_tblout,
        METH_VARARGS,
        "Remove a record. If there are duplicates only the first is removed."
    },
    
    {
        "tblget", (PyCFunction) Tyrant_tblget,
        METH_VARARGS,
        "Retrieve a record. If none is found None or the supplied default value is returned."
    },
    
    {
        "tblsetindex", (PyCFunction) Tyrant_tblsetindex,
        METH_VARARGS,
        "Set an index on a column."
    },
    
    {
        "tblgenuid", (PyCFunction) Tyrant_tblgenuid,
        METH_VARARGS,
        "Generate a unique record id."
    },
    
    {
        "tblquery", (PyCFunction) Tyrant_tblquery,
        METH_NOARGS,
        "Get a query object for this database."
    },
    
    {
        "metasearch", (PyCFunction) Tyrant_metasearch,
        METH_VARARGS,
        "Use multiple query objects and a set operation to retrieve records."
    },
    
    { NULL }
};


static PyTypeObject TyrantType = {
  PyObject_HEAD_INIT(NULL)
  0,                                           /* ob_size */
  "tokyocabinet.tyrant.Tyrant",                /* tp_name */
  sizeof(Tyrant),                              /* tp_basicsize */
  0,                                           /* tp_itemsize */
  (destructor)Tyrant_dealloc,                  /* tp_dealloc */
  0,                                           /* tp_print */
  0,                                           /* tp_getattr */
  0,                                           /* tp_setattr */
  0,                                           /* tp_compare */
  0,                                           /* tp_repr */
  0,                                           /* tp_as_number */
  &Tyrant_as_sequence,                         /* tp_as_sequence */
  &Tyrant_as_mapping,                          /* tp_as_mapping */
  Tyrant_Hash,                                 /* tp_hash  */
  0,                                           /* tp_call */
  0,                                           /* tp_str */
  0,                                           /* tp_getattro */
  0,                                           /* tp_setattro */
  0,                                           /* tp_as_buffer */
  Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,    /* tp_flags */
  "Tyrant database",                           /* tp_doc */
  0,                                           /* tp_traverse */
  0,                                           /* tp_clear */
  0,                                           /* tp_richcompare */
  0,                                           /* tp_weaklistoffset */
  0,                                           /* tp_iter */
  0,                                           /* tp_iternext */
  Tyrant_methods,                              /* tp_methods */
  0,                                           /* tp_members */
  0,                                           /* tp_getset */
  0,                                           /* tp_base */
  0,                                           /* tp_dict */
  0,                                           /* tp_descr_get */
  0,                                           /* tp_descr_set */
  0,                                           /* tp_dictoffset */
  0,                                           /* tp_init */
  0,                                           /* tp_alloc */
  Tyrant_new,                                  /* tp_new */
};


#define ADD_INT_CONSTANT(module, CONSTANT) PyModule_AddIntConstant(module, #CONSTANT, CONSTANT)

#ifndef PyMODINIT_FUNC
#define PyMODINIT_FUNC void
#endif
PyMODINIT_FUNC
inittokyotyrant(void)
{
    PyObject *m;
    
    m = Py_InitModule3(
            "tokyotyrant", NULL, 
            "Tokyo Tyrant client wrapper"
    );
    
    if (!m)
    {
        return;
    }
    
    TyrantError = PyErr_NewException("tokyotyrant.error", NULL, NULL);
    Py_INCREF(TyrantError);
    PyModule_AddObject(m, "error", TyrantError);
    
    if (PyType_Ready(&TyrantType) < 0)
    {
        return;
    }
    
    if (PyType_Ready(&TyrantQueryType) < 0)
    {
        return;
    }
    
    Py_INCREF(&TyrantType);
    PyModule_AddObject(m, "Tyrant", (PyObject *) &TyrantType);
    
    Py_INCREF(&TyrantQueryType);
    PyModule_AddObject(m, "TyrantQuery", (PyObject *) &TyrantQueryType);
    
    ADD_INT_CONSTANT(m, RDBROCHKCON);
    
    ADD_INT_CONSTANT(m, RDBITLEXICAL);
    ADD_INT_CONSTANT(m, RDBITDECIMAL);
    ADD_INT_CONSTANT(m, RDBITTOKEN);
    ADD_INT_CONSTANT(m, RDBITQGRAM);
    ADD_INT_CONSTANT(m, RDBITOPT);
    ADD_INT_CONSTANT(m, RDBITVOID);
    ADD_INT_CONSTANT(m, RDBITKEEP);
    
    ADD_INT_CONSTANT(m, RDBQCSTREQ);
    ADD_INT_CONSTANT(m, RDBQCSTRINC);
    ADD_INT_CONSTANT(m, RDBQCSTRBW);
    ADD_INT_CONSTANT(m, RDBQCSTREW);
    ADD_INT_CONSTANT(m, RDBQCSTRAND);
    ADD_INT_CONSTANT(m, RDBQCSTROR);
    ADD_INT_CONSTANT(m, RDBQCSTROREQ);
    ADD_INT_CONSTANT(m, RDBQCSTRRX);
    ADD_INT_CONSTANT(m, RDBQCNUMEQ);
    ADD_INT_CONSTANT(m, RDBQCNUMGT);
    ADD_INT_CONSTANT(m, RDBQCNUMGE);
    ADD_INT_CONSTANT(m, RDBQCNUMLT);
    ADD_INT_CONSTANT(m, RDBQCNUMLE);
    ADD_INT_CONSTANT(m, RDBQCNUMBT);
    ADD_INT_CONSTANT(m, RDBQCNUMOREQ);
    ADD_INT_CONSTANT(m, RDBQCFTSPH);
    ADD_INT_CONSTANT(m, RDBQCFTSAND);
    ADD_INT_CONSTANT(m, RDBQCFTSOR);
    ADD_INT_CONSTANT(m, RDBQCFTSEX);
    ADD_INT_CONSTANT(m, RDBQCNEGATE);
    ADD_INT_CONSTANT(m, RDBQCNOIDX);
    
    ADD_INT_CONSTANT(m, RDBQOSTRASC);
    ADD_INT_CONSTANT(m, RDBQOSTRDESC);
    ADD_INT_CONSTANT(m, RDBQONUMASC);
    ADD_INT_CONSTANT(m, RDBQONUMDESC);
    
    ADD_INT_CONSTANT(m, RDBMSUNION);
    ADD_INT_CONSTANT(m, RDBMSISECT);
    ADD_INT_CONSTANT(m, RDBMSDIFF);
}
