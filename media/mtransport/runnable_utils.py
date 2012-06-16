MAX_ARGS = 10

def gen_args_type(args):
    ret = []
    for arg in range(0, args):
        ret.append("A%d a%d"%(arg, arg))
    return ", ".join(ret)

def gen_args(args):
    ret = []
    for arg in range(0, args):
        ret.append("a%d"%(arg))
    return ", ".join(ret)

def gen_args_(args):
    ret = []
    for arg in range(0, args):
        ret.append("a%d_"%(arg))
    return ", ".join(ret)

def gen_init(args):
    ret = []
    for arg in range(0, args):
        ret.append("a%d_(a%d)"%(arg, arg))
    return ", ".join(ret)

def gen_typenames(args):
    ret = []
    for arg in range(0, args):
        ret.append("typename A%d"%(arg))
    return ", ".join(ret)

def gen_types(args):
    ret = []
    for arg in range(0, args):
        ret.append("A%d"%(arg))
    return ", ".join(ret)

    
def generate_class_template(args, ret = False):
    print "// %d arguments --"%args
    if not ret:
        print "template<typename C, typename M, "+ gen_typenames(args) + "> class runnable_args%d : public runnable_args_base {"%args
    else:
        print "template<typename C, typename M, "+ gen_typenames(args) + ", typename R> class runnable_args%d_ret : public runnable_args_base {"%args

    print " public:"

    if not ret:
        print "  runnable_args%d(C o, M m, "%args + gen_args_type(args) + ") :"
        print "    o_(o), m_(m), " + gen_init(args) + "  {}"
    else:
        print "  runnable_args%d_ret(C o, M m, "%args + gen_args_type(args) + ", R *r) :"
        print "    o_(o), m_(m), r_(r), " + gen_init(args) + "  {}"
    print
    print "  NS_IMETHOD Run() {"
    if not ret:
        print "    ((*o_).*m_)(" + gen_args_(args) + ");"
    else:
        print "    *r_ = ((*o_).*m_)(" + gen_args_(args) + ");"        
    print "    return NS_OK;"
    print "  }"
    print
    print " private:"
    print "  C o_;"
    print "  M m_;"
    if ret:
        print "  R* r_;"
    for arg in range(0, args):
        print "  A%d a%d_;"%(arg, arg)
    print "};"
    print
    print
    print

def generate_function_template(args):
    print "// %d arguments --"%args
    print "template<typename C, typename M, "+ gen_typenames(args) + ">"
    print "runnable_args%d<C, M, "%args + gen_types(args) + ">* WrapRunnable(C o, M m, " + gen_args_type(args) + ") {"
    print "  return new runnable_args%d<C, M, "%args + gen_types(args) + ">"
    print "    (o, m, " + gen_args(args) + ");"
    print "}"
    print

def generate_function_template_ret(args):
    print "// %d arguments --"%args
    print "template<typename C, typename M, "+ gen_typenames(args) + ", typename R>"
    print "runnable_args%d_ret<C, M, "%args + gen_types(args) + ", R>* WrapRunnableRet(C o, M m, " + gen_args_type(args) + ", R* r) {"
    print "  return new runnable_args%d_ret<C, M, "%args + gen_types(args) + ", R>"
    print "    (o, m, " + gen_args(args) + ", r);"
    print "}"
    print

for num_args in range (1, MAX_ARGS):
    generate_class_template(num_args)
    generate_class_template(num_args, True)

print
print
print

for num_args in range(1, MAX_ARGS):
    generate_function_template(num_args)
    generate_function_template_ret(num_args)
    



