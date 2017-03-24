#include "pycall_ext.h"

#include <assert.h>

static VALUE mPyCall;
static VALUE mLibPython;
static VALUE cPyPtr;
static VALUE rbffi_PointerClass;

static ID id_incref;
static ID id_to_ptr;

static void (*Py_IncRef)(PyObject *);
static void (*Py_DecRef)(PyObject *);

enum pyptr_flags {
  PYPTR_NEED_DECREF = 1
};

typedef struct pyptr_struct {
  PyObject *pyobj;
  VALUE flags;
} pyptr_t;

#define PYPTR(p) ((pyptr_t *)(p))
#define PYPTR_PYOBJ(p) (PYPTR(p)->pyobj)
#define PYPTR_FLAGS(p) (PYPTR(p)->flags)

#define PYPTR_DECREF_NEEDED_P(p) (0 != (PYPTR_FLAGS(p) & PYPTR_NEED_DECREF))

static inline int
ffi_pointer_p(VALUE obj)
{
  return CLASS_OF(obj) == rbffi_PointerClass;
}

static void
pyptr_mark(void *ptr)
{
}

static void
pyptr_free(void *ptr)
{
  if (PYPTR_PYOBJ(ptr) && PYPTR_DECREF_NEEDED_P(ptr)) {
    Py_DecRef(PYPTR_PYOBJ(ptr));
  }
  xfree(ptr);
}

static size_t
pyptr_memsize(void const *ptr)
{
  return sizeof(pyptr_t);
}

static rb_data_type_t const pyptr_data_type = {
  "pyptr",
  {
    pyptr_mark,
    pyptr_free,
    pyptr_memsize,
  },
  0, 0, RUBY_TYPED_FREE_IMMEDIATELY
};

static VALUE
pyptr_alloc(VALUE klass)
{
  pyptr_t *pyptr;
  VALUE obj = TypedData_Make_Struct(klass, pyptr_t, &pyptr_data_type, pyptr);
  pyptr->pyobj = NULL;
  pyptr->flags = 0;
  return obj;
}

static void
pyptr_init(VALUE obj, PyObject *pyobj, int incref, int decref)
{
  pyptr_t *pyptr;

  assert(Py_IncRef != NULL);
  assert(Py_DecRef != NULL);

  TypedData_Get_Struct(obj, pyptr_t, &pyptr_data_type, pyptr);

  pyptr->pyobj = pyobj;

  if (incref) {
    Py_IncRef(pyobj);
  }

  if (decref) {
    pyptr->flags |= PYPTR_NEED_DECREF;
  }
}

static VALUE
pyptr_initialize(int argc, VALUE *argv, VALUE self)
{
  VALUE ptr_like, incref;
  void *address;

  switch (rb_scan_args(argc, argv, "11", &ptr_like, &incref)) {
    case 1:
      incref = Qtrue;
      break;

    default:
      break;
  }

  if (RB_INTEGER_TYPE_P(ptr_like)) {
    address = (void *)NUM2SIZET(ptr_like);
  }
  else if (ffi_pointer_p(ptr_like)) {
    Pointer *ffi_ptr;
ffi_pointer:
    Data_Get_Struct(ptr_like, Pointer, ffi_ptr);
    address = ffi_ptr->memory.address;
  }
  else if (rb_respond_to(ptr_like, id_to_ptr)) {
    ptr_like = rb_funcall2(ptr_like, id_to_ptr, 0, NULL);
    goto ffi_pointer;
  }
  else {
    rb_raise(rb_eTypeError, "The argument must be either Integer or FFI::Pointer-like object.");
  }

  pyptr_init(self, (PyObject *)address, RTEST(incref), 1);

  return self;
}

static VALUE
pyptr_get_refcnt(VALUE self)
{
  pyptr_t *pyptr;
  TypedData_Get_Struct(self, pyptr_t, &pyptr_data_type, pyptr);
  return SSIZET2NUM(PYPTR_PYOBJ(pyptr)->ob_refcnt);
}

static VALUE
pyptr_get_address(VALUE self)
{
  pyptr_t *pyptr;
  TypedData_Get_Struct(self, pyptr_t, &pyptr_data_type, pyptr);
  return rb_uint_new((VALUE)PYPTR_PYOBJ(pyptr));
}

void
Init_pycall_ext(void)
{
  rb_require("ffi");
  rbffi_PointerClass = rb_path2class("FFI::Pointer");

  mPyCall = rb_define_module("PyCall");
  mLibPython = rb_define_module_under(mPyCall, "LibPython");
  cPyPtr = rb_define_class_under(mLibPython, "PyPtr", rb_cObject);
  rb_define_alloc_func(cPyPtr, pyptr_alloc);
  rb_define_method(cPyPtr, "initialize", pyptr_initialize, -1);
  rb_define_method(cPyPtr, "__refcnt__", pyptr_get_refcnt, 0);
  rb_define_method(cPyPtr, "__address__", pyptr_get_address, 0);

  id_incref = rb_intern("incref");
  id_to_ptr = rb_intern("to_ptr");
}