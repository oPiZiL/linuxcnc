// Support for Python oword subroutines
//
// proof-of-concept for access to Interp and canon
//
// NB: all this is executed at readahead time
//
// Michael Haberler 4/2011
//
// if you get a segfault like described
// here: https://bugs.launchpad.net/ubuntu/+source/mesa/+bug/259219
// or here: https://www.libavg.de/wiki/LinuxInstallIssues#glibc_invalid_pointer :
//
// try this before starting milltask and axis in emc:
//  LD_PRELOAD=/usr/lib/libstdc++.so.6 $EMCTASK ...
//  LD_PRELOAD=/usr/lib/libstdc++.so.6 $EMCDISPLAY ...
// this is actually a bug in libgl1-mesa-dri and it looks
// it has been fixed in mesa - 7.10.1-0ubuntu2

#include <boost/python.hpp>
#include <boost/make_shared.hpp>
#include <boost/python/object.hpp>
#include <boost/python/raw_function.hpp>
#include <boost/python/call.hpp>

namespace bp = boost::python;

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "rs274ngc.hh"
#include "interp_return.hh"
#include "interp_internal.hh"
#include "rs274ngc_interp.hh"
#include "units.h"

#define PYCHK(bad, fmt, ...)				       \
    do {                                                       \
        if (bad) {                                             \
	    ERS(fmt, ## __VA_ARGS__);                          \
	    goto error;					       \
        }                                                      \
    } while(0)

// accessing the private data of Interp is kind of kludgy because
// technically the Python module does not run in a member context
// but 'outside'
// we store references to private data during call setup and
// use it to reference it in the python module
// NB: this is likely not thread-safe
static setup_pointer current_setup;

// http://hafizpariabi.blogspot.com/2008/01/using-custom-deallocator-in.html
// reason: avoid segfaults by del(interp_instance) on program exit
// make delete(interp_instance) a noop wrt Interp
static void interpDeallocFunc(Interp *interp)
{
    fprintf(stderr,"----> interpDeallocFunc pid=%d\n",getpid());
}
typedef boost::shared_ptr< Interp > interp_ptr;
static interp_ptr  interp_instance;
static Interp *current_interp;

// accessors for a few _setup members so they can be injected into Python
// this would be much easier with add_static_property but _setup
// would have to be public
static int get_selected_pocket() { return get_setup(current_interp)->selected_pocket; }
static void set_selected_pocket(int p) { current_setup->selected_pocket = p; }
static int get_current_pocket() { return current_setup->current_pocket; }
static void set_current_pocket(int p) { current_setup->current_pocket = p; }
static int get_toolchange_flag() { return current_setup->toolchange_flag; }
static void set_toolchange_flag(bool f) { current_setup->toolchange_flag = f; }
static double get_return_value() { return current_setup->return_value; }
static int get_remap_level() { return  current_setup->stack_level; }
const char *blocktext() { return &current_setup->blocktext[0]; }

#define IS_STRING(x) (PyObject_IsInstance(x.ptr(), (PyObject*)&PyString_Type))
#define IS_INT(x) (PyObject_IsInstance(x.ptr(), (PyObject*)&PyInt_Type))


struct ParamClass
{
    double getitem(bp::object sub)
    {
	double retval = 0;
	if (IS_STRING(sub)) {
	    char const* varname = bp::extract < char const* > (sub);
	    fprintf(stderr,"getitem('%s')\n",varname);

	} else
	    if (IS_INT(sub)) {
		int index = bp::extract < int > (sub);
		retval = current_setup->parameters[index];
		// fprintf(stderr,"return parameters[%d] = %f\n",index,retval);
	    } else {
		fprintf(stderr,"getitem:bad subscript type\n");
		throw std::runtime_error("getitem: bad subscript type, neither int nor string");
	    }
	return retval;
    }

    double setitem(bp::object sub, double dvalue)
    {
	if (IS_STRING(sub)) {
	    char const* varname = bp::extract < char const* > (sub);
	    fprintf(stderr,"setitem('%s'), %f\n",varname,dvalue);

	} else
	    if (IS_INT(sub)) {
		int index = bp::extract < int > (sub);
		current_setup->parameters[index] = dvalue;
		// fprintf(stderr,"set parameters[%d] = %f\n",index,dvalue);
		return dvalue;
	    } else
		fprintf(stderr,"setitem BAD SUBSCRIPT TYPE\n");

	return dvalue;
    }
};

static ParamClass paramclass;


BOOST_PYTHON_MODULE(InterpMod) {
    using namespace boost::python;
    using namespace boost;

    scope().attr("__doc__") =
        "Interpreter introspection\n"
        ;

    class_< Interp, interp_ptr,
        noncopyable >("Interp",no_init)

	.def("sequence_number", &Interp::sequence_number)
	.def("load_tool_table", &Interp::load_tool_table)
	.def("synch", &Interp::synch)

	.add_property("call_level", &Interp::call_level)

	.add_static_property("selected_pocket", &::get_selected_pocket,&::set_selected_pocket)
	.add_static_property("current_pocket", &::get_current_pocket, &::set_current_pocket)
	.add_static_property("toolchange_flag", &::get_toolchange_flag, &::set_toolchange_flag)
	.add_static_property("remap_level", &get_remap_level)
	.add_static_property("return_value", &get_return_value)
	;
    class_<ParamClass,noncopyable>("ParamClass","Interpreter parameters",no_init)
	.def("__getitem__", &ParamClass::getitem)
	.def("__setitem__", &ParamClass::setitem);
    ;

    //.def("execute", &Interp::execute,execute_overloads()); //??
}

BOOST_PYTHON_MODULE(CanonMod) {
    using namespace boost::python;

    // first stab - result of an awk-based mass conversion.
    // just keeping those without compile errors (no missing type converters)
    def("ARC_FEED",&ARC_FEED);
    //def("CANON_AXIS;",&CANON_AXIS);
    //def("CANON_CONTINUOUS.",&CANON_CONTINUOUS);
    //def("CANON_DIRECTION;",&CANON_DIRECTION);
    //def("CANON_FEED_REFERENCE;",&CANON_FEED_REFERENCE);
    //def("CANON_MOTION_MODE;",&CANON_MOTION_MODE);
    //def("CANON_PLANE;",&CANON_PLANE);
    //def("CANON_SIDE;",&CANON_SIDE);
    //def("CANON_SPEED_FEED_MODE;",&CANON_SPEED_FEED_MODE);
    //def("CANON_UNITS;",&CANON_UNITS);
    // def("CANON_UPDATE_END_POINT",&CANON_UPDATE_END_POINT);
    def("CHANGE_TOOL",&CHANGE_TOOL);
    def("CHANGE_TOOL_NUMBER",&CHANGE_TOOL_NUMBER);
    def("CLAMP_AXIS",&CLAMP_AXIS);
    def("CLEAR_AUX_OUTPUT_BIT",&CLEAR_AUX_OUTPUT_BIT);
    def("CLEAR_MOTION_OUTPUT_BIT",&CLEAR_MOTION_OUTPUT_BIT);
    def("COMMENT",&COMMENT);
    def("DISABLE_ADAPTIVE_FEED",&DISABLE_ADAPTIVE_FEED);
    def("DISABLE_FEED_HOLD",&DISABLE_FEED_HOLD);
    def("DISABLE_FEED_OVERRIDE",&DISABLE_FEED_OVERRIDE);
    def("DISABLE_SPEED_OVERRIDE",&DISABLE_SPEED_OVERRIDE);
    def("DWELL",&DWELL);
    def("ENABLE_ADAPTIVE_FEED",&ENABLE_ADAPTIVE_FEED);
    def("ENABLE_FEED_HOLD",&ENABLE_FEED_HOLD);
    def("ENABLE_FEED_OVERRIDE",&ENABLE_FEED_OVERRIDE);
    def("ENABLE_SPEED_OVERRIDE",&ENABLE_SPEED_OVERRIDE);
    def("FINISH",&FINISH);
    def("FLOOD_OFF",&FLOOD_OFF);
    def("FLOOD_ON",&FLOOD_ON);
    def("GET_BLOCK_DELETE",&GET_BLOCK_DELETE);
    def("GET_EXTERNAL_ADAPTIVE_FEED_ENABLE",&GET_EXTERNAL_ADAPTIVE_FEED_ENABLE);
    def("GET_EXTERNAL_ANALOG_INPUT",&GET_EXTERNAL_ANALOG_INPUT);
    //    def("GET_EXTERNAL_ANGLE_UNIT_FACTOR",&GET_EXTERNAL_ANGLE_UNIT_FACTOR);
    def("GET_EXTERNAL_ANGLE_UNITS",&GET_EXTERNAL_ANGLE_UNITS);
    def("GET_EXTERNAL_AXIS_MASK",&GET_EXTERNAL_AXIS_MASK);
    def("GET_EXTERNAL_DIGITAL_INPUT",&GET_EXTERNAL_DIGITAL_INPUT);
    def("GET_EXTERNAL_FEED_HOLD_ENABLE",&GET_EXTERNAL_FEED_HOLD_ENABLE);
    def("GET_EXTERNAL_FEED_OVERRIDE_ENABLE",&GET_EXTERNAL_FEED_OVERRIDE_ENABLE);
    def("GET_EXTERNAL_FEED_RATE",&GET_EXTERNAL_FEED_RATE);
    def("GET_EXTERNAL_FLOOD",&GET_EXTERNAL_FLOOD);
    //    def("GET_EXTERNAL_LENGTH_UNIT_FACTOR",&GET_EXTERNAL_LENGTH_UNIT_FACTOR);
    def("GET_EXTERNAL_LENGTH_UNITS",&GET_EXTERNAL_LENGTH_UNITS);
    def("GET_EXTERNAL_MIST",&GET_EXTERNAL_MIST);
    def("GET_EXTERNAL_MOTION_CONTROL_MODE",&GET_EXTERNAL_MOTION_CONTROL_MODE);
    def("GET_EXTERNAL_MOTION_CONTROL_TOLERANCE",&GET_EXTERNAL_MOTION_CONTROL_TOLERANCE);
    // def("GET_EXTERNAL_ORIGIN_A",&GET_EXTERNAL_ORIGIN_A);
    // def("GET_EXTERNAL_ORIGIN_B",&GET_EXTERNAL_ORIGIN_B);
    // def("GET_EXTERNAL_ORIGIN_C",&GET_EXTERNAL_ORIGIN_C);
    // def("GET_EXTERNAL_ORIGIN_X",&GET_EXTERNAL_ORIGIN_X);
    // def("GET_EXTERNAL_ORIGIN_Y",&GET_EXTERNAL_ORIGIN_Y);
    // def("GET_EXTERNAL_ORIGIN_Z",&GET_EXTERNAL_ORIGIN_Z);
    def("GET_EXTERNAL_PARAMETER_FILE_NAME",&GET_EXTERNAL_PARAMETER_FILE_NAME);
    def("GET_EXTERNAL_PLANE",&GET_EXTERNAL_PLANE);
    def("GET_EXTERNAL_POCKETS_MAX",&GET_EXTERNAL_POCKETS_MAX);
    def("GET_EXTERNAL_POSITION_A",&GET_EXTERNAL_POSITION_A);
    def("GET_EXTERNAL_POSITION_B",&GET_EXTERNAL_POSITION_B);
    def("GET_EXTERNAL_POSITION_C",&GET_EXTERNAL_POSITION_C);
    def("GET_EXTERNAL_POSITION_U",&GET_EXTERNAL_POSITION_U);
    def("GET_EXTERNAL_POSITION_V",&GET_EXTERNAL_POSITION_V);
    def("GET_EXTERNAL_POSITION_W",&GET_EXTERNAL_POSITION_W);
    def("GET_EXTERNAL_POSITION_X",&GET_EXTERNAL_POSITION_X);
    def("GET_EXTERNAL_POSITION_Y",&GET_EXTERNAL_POSITION_Y);
    def("GET_EXTERNAL_POSITION_Z",&GET_EXTERNAL_POSITION_Z);
    def("GET_EXTERNAL_PROBE_POSITION_A",&GET_EXTERNAL_PROBE_POSITION_A);
    def("GET_EXTERNAL_PROBE_POSITION_B",&GET_EXTERNAL_PROBE_POSITION_B);
    def("GET_EXTERNAL_PROBE_POSITION_C",&GET_EXTERNAL_PROBE_POSITION_C);
    def("GET_EXTERNAL_PROBE_POSITION_U",&GET_EXTERNAL_PROBE_POSITION_U);
    def("GET_EXTERNAL_PROBE_POSITION_V",&GET_EXTERNAL_PROBE_POSITION_V);
    def("GET_EXTERNAL_PROBE_POSITION_W",&GET_EXTERNAL_PROBE_POSITION_W);
    def("GET_EXTERNAL_PROBE_POSITION_X",&GET_EXTERNAL_PROBE_POSITION_X);
    def("GET_EXTERNAL_PROBE_POSITION_Y",&GET_EXTERNAL_PROBE_POSITION_Y);
    def("GET_EXTERNAL_PROBE_POSITION_Z",&GET_EXTERNAL_PROBE_POSITION_Z);
    def("GET_EXTERNAL_PROBE_TRIPPED_VALUE",&GET_EXTERNAL_PROBE_TRIPPED_VALUE);
    def("GET_EXTERNAL_PROBE_VALUE",&GET_EXTERNAL_PROBE_VALUE);
    def("GET_EXTERNAL_QUEUE_EMPTY",&GET_EXTERNAL_QUEUE_EMPTY);
    def("GET_EXTERNAL_SELECTED_TOOL_SLOT",&GET_EXTERNAL_SELECTED_TOOL_SLOT);
    def("GET_EXTERNAL_SPEED",&GET_EXTERNAL_SPEED);
    def("GET_EXTERNAL_SPINDLE",&GET_EXTERNAL_SPINDLE);
    def("GET_EXTERNAL_SPINDLE_OVERRIDE_ENABLE",&GET_EXTERNAL_SPINDLE_OVERRIDE_ENABLE);
    def("GET_EXTERNAL_TC_FAULT",&GET_EXTERNAL_TC_FAULT);
    def("GET_EXTERNAL_TOOL_LENGTH_AOFFSET",&GET_EXTERNAL_TOOL_LENGTH_AOFFSET);
    def("GET_EXTERNAL_TOOL_LENGTH_BOFFSET",&GET_EXTERNAL_TOOL_LENGTH_BOFFSET);
    def("GET_EXTERNAL_TOOL_LENGTH_COFFSET",&GET_EXTERNAL_TOOL_LENGTH_COFFSET);
    def("GET_EXTERNAL_TOOL_LENGTH_UOFFSET",&GET_EXTERNAL_TOOL_LENGTH_UOFFSET);
    def("GET_EXTERNAL_TOOL_LENGTH_VOFFSET",&GET_EXTERNAL_TOOL_LENGTH_VOFFSET);
    def("GET_EXTERNAL_TOOL_LENGTH_WOFFSET",&GET_EXTERNAL_TOOL_LENGTH_WOFFSET);
    def("GET_EXTERNAL_TOOL_LENGTH_XOFFSET",&GET_EXTERNAL_TOOL_LENGTH_XOFFSET);
    def("GET_EXTERNAL_TOOL_LENGTH_YOFFSET",&GET_EXTERNAL_TOOL_LENGTH_YOFFSET);
    def("GET_EXTERNAL_TOOL_LENGTH_ZOFFSET",&GET_EXTERNAL_TOOL_LENGTH_ZOFFSET);
    def("GET_EXTERNAL_TOOL_SLOT",&GET_EXTERNAL_TOOL_SLOT);
    def("GET_EXTERNAL_TOOL_TABLE",&GET_EXTERNAL_TOOL_TABLE);
    def("GET_EXTERNAL_TRAVERSE_RATE",&GET_EXTERNAL_TRAVERSE_RATE);
    def("GET_OPTIONAL_PROGRAM_STOP",&GET_OPTIONAL_PROGRAM_STOP);
    def("INIT_CANON",&INIT_CANON);
    def("INTERP_ABORT",&INTERP_ABORT);
    def("LOCK_ROTARY",&LOCK_ROTARY);
    //    def("LOCK_SPINDLE_Z",&LOCK_SPINDLE_Z);
    def("LOGAPPEND",&LOGAPPEND);
    def("LOGCLOSE",&LOGCLOSE);
    def("LOG",&LOG);
    def("LOGOPEN",&LOGOPEN);
    def("MESSAGE",&MESSAGE);
    def("MIST_OFF",&MIST_OFF);
    def("MIST_ON",&MIST_ON);
    // def("NURB_CONTROL_POINT",&NURB_CONTROL_POINT);
    //  def("NURB_FEED",&NURB_FEED);
    // def("NURB_KNOT_VECTOR",&NURB_KNOT_VECTOR);
    def("NURBS_FEED",&NURBS_FEED);
    def("OPTIONAL_PROGRAM_STOP",&OPTIONAL_PROGRAM_STOP);
    // def("ORIENT_SPINDLE",&ORIENT_SPINDLE);
    def("PALLET_SHUTTLE",&PALLET_SHUTTLE);
    def("PROGRAM_END",&PROGRAM_END);
    def("PROGRAM_STOP",&PROGRAM_STOP);
    def("RIGID_TAP",&RIGID_TAP);
    def("SELECT_PLANE",&SELECT_PLANE);
    def("SELECT_POCKET",&SELECT_POCKET);
    def("SET_AUX_OUTPUT_BIT",&SET_AUX_OUTPUT_BIT);
    def("SET_AUX_OUTPUT_VALUE",&SET_AUX_OUTPUT_VALUE);
    def("SET_BLOCK_DELETE",&SET_BLOCK_DELETE);
    def("SET_CUTTER_RADIUS_COMPENSATION",&SET_CUTTER_RADIUS_COMPENSATION);
    def("SET_FEED_MODE",&SET_FEED_MODE);
    def("SET_FEED_RATE",&SET_FEED_RATE);
    //    def("SET_FEED_REFERENCE",&SET_FEED_REFERENCE);
    def("SET_G5X_OFFSET",&SET_G5X_OFFSET);
    def("SET_G92_OFFSET",&SET_G92_OFFSET);
    //   def("SET_MOTION_CONTROL_MODE",&SET_MOTION_CONTROL_MODE);
    def("SET_MOTION_OUTPUT_BIT",&SET_MOTION_OUTPUT_BIT);
    def("SET_MOTION_OUTPUT_VALUE",&SET_MOTION_OUTPUT_VALUE);
    def("SET_NAIVECAM_TOLERANCE",&SET_NAIVECAM_TOLERANCE);
    def("SET_OPTIONAL_PROGRAM_STOP",&SET_OPTIONAL_PROGRAM_STOP);
    def("SET_SPINDLE_MODE",&SET_SPINDLE_MODE);
    def("SET_SPINDLE_SPEED",&SET_SPINDLE_SPEED);
    def("SET_TOOL_TABLE_ENTRY",&SET_TOOL_TABLE_ENTRY);
    def("SET_TRAVERSE_RATE",&SET_TRAVERSE_RATE);
    def("SET_XY_ROTATION",&SET_XY_ROTATION);
    def("SPINDLE_RETRACT",&SPINDLE_RETRACT);
    def("SPINDLE_RETRACT_TRAVERSE",&SPINDLE_RETRACT_TRAVERSE);
    def("START_CHANGE",&START_CHANGE);
    def("START_CUTTER_RADIUS_COMPENSATION",&START_CUTTER_RADIUS_COMPENSATION);
    def("START_SPEED_FEED_SYNCH",&START_SPEED_FEED_SYNCH);
    def("START_SPINDLE_CLOCKWISE",&START_SPINDLE_CLOCKWISE);
    def("START_SPINDLE_COUNTERCLOCKWISE",&START_SPINDLE_COUNTERCLOCKWISE);
    def("STOP_CUTTER_RADIUS_COMPENSATION",&STOP_CUTTER_RADIUS_COMPENSATION);
    def("STOP_SPEED_FEED_SYNCH",&STOP_SPEED_FEED_SYNCH);
    def("STOP_SPINDLE_TURNING",&STOP_SPINDLE_TURNING);
    // def("STOP",&STOP);
    def("STRAIGHT_FEED",&STRAIGHT_FEED);
    def("STRAIGHT_PROBE",&STRAIGHT_PROBE);
    def("STRAIGHT_TRAVERSE",&STRAIGHT_TRAVERSE);
    def("TURN_PROBE_OFF",&TURN_PROBE_OFF);
    def("TURN_PROBE_ON",&TURN_PROBE_ON);
    // def("UNCLAMP_AXIS",&UNCLAMP_AXIS);
    def("UNLOCK_ROTARY",&UNLOCK_ROTARY);
    def("USE_LENGTH_UNITS",&USE_LENGTH_UNITS);
    def("USE_NO_SPINDLE_FORCE",&USE_NO_SPINDLE_FORCE);
    // def("USER_DEFINED_FUNCTION_ADD",&USER_DEFINED_FUNCTION_ADD);
    // def("USE_SPINDLE_FORCE",&USE_SPINDLE_FORCE);
    def("USE_TOOL_LENGTH_OFFSET",&USE_TOOL_LENGTH_OFFSET);
    def("WAIT",&WAIT);
    //    def("XYZ",&XYZ);
}


std::string handle_pyerror()
{
    using namespace boost::python;
    using namespace boost;

    PyObject *exc,*val,*tb;
    PyErr_Fetch(&exc,&val,&tb);
    handle<> hexc(exc),hval(val),htb(tb);
    if(!htb || !hval) {
	fprintf(stderr, "handle_pyerror: BUG \n");
	// MYAPP_ASSERT_BUG(hexc);
	return extract<std::string>(str(hexc));
    } else {
	object traceback(import("traceback"));
	object format_exception(traceback.attr("format_exception"));
	object formatted_list(format_exception(hexc,hval,htb));
	object formatted(str("\n").join(formatted_list));
	return extract<std::string>(formatted);
    }
}

int Interp::init_python(setup_pointer settings)
{
    char cmd[LINELEN];
    char *path;
    PyGILState_STATE gstate;

    //Log("get_setup().selected_pocket=%d",get_setup().selected_pocket);
    current_setup = get_setup(this); // // &this->_setup;
    current_interp = this;

    if (settings->pymodule_stat != PYMOD_NONE) {
	logPy("init_python RE-INIT %d",settings->pymodule_stat);
	return INTERP_OK;  // already done, or failed
    }

    logPy("init_python(this=%lx  pid=%d pymodule_stat=%d PyInited=%d",
	  (unsigned long)this,getpid(),settings->pymodule_stat,Py_IsInitialized());


    // boost::shared_ptr< Interp, interpDeallocFunc > interp_ptr;
    interp_instance = interp_ptr(this,interpDeallocFunc  );

    PYCHK((settings->pymodule == NULL),
	  "init_python: no module defined");

    if (settings->pydir) {
	snprintf(cmd,sizeof(cmd),"%s/%s", settings->pydir,settings->pymodule);
    } else
	strcpy(cmd,settings->pymodule);

    PYCHK(((path = realpath(cmd,NULL)) == NULL),
	  "init_python: can resolve path to '%s'",cmd);
    logPy("module path='%s'",path);

    Py_SetProgramName(path);
    PyImport_AppendInittab( (char *) "InterpMod", &initInterpMod);
    PyImport_AppendInittab( (char *) "CanonMod", &initCanonMod);
    Py_Initialize();
    // gstate = PyGILState_Ensure();

    try {
	settings->module = bp::import("__main__");
	settings->module_namespace = settings->module.attr("__dict__");

	bp::object interp_module = bp::import("InterpMod");

	bp::scope(interp_module).attr("interp") = interp_instance;
	bp::scope(interp_module).attr("params") = bp::ptr(&paramclass);

	settings->module_namespace["InterpMod"] = interp_module;

	bp::object canon_module = bp::import("CanonMod");
	settings->module_namespace["CanonMod"] = canon_module;



	bp::object result = bp::exec_file(path,
					  settings->module_namespace,
					  settings->module_namespace);


	settings->pymodule_stat = PYMOD_OK;
    }
    catch (bp::error_already_set) {
	std::string msg = handle_pyerror();

	logPy("init_python: module '%s' init failed: %s",path,msg.c_str());
	//free(path);
	// PyGILState_Release(gstate);

	ERS("init_python: %s",msg.c_str());
	settings->pymodule_stat = PYMOD_FAILED;
	PyErr_Clear();
    }
    //free(path);
    //PyGILState_Release(gstate);


    return INTERP_OK;

 error:  // come here if PYCHK() macro fails
    return INTERP_ERROR;
}

bool Interp::is_pycallable(setup_pointer settings,
			   const char *funcname)
{
    bool result = false;

    if ((settings->pymodule_stat != PYMOD_OK) ||
	(funcname == NULL)) {
	return false;
    }

    try {
	bp::object function = settings->module_namespace[funcname];
	result = PyCallable_Check(function.ptr());
    }
    catch (bp::error_already_set) {
	// KeyError expected if not callable
	if (!PyErr_ExceptionMatches(PyExc_KeyError)) {
	    // something else, strange
	    std::string msg = handle_pyerror();
	    logPy("is_pycallable: %s",msg.c_str());
	}
	result = false;
	PyErr_Clear();
    }
    logPy("py_iscallable(%s) = %s\n",funcname,result ? "TRUE":"FALSE");
    return result;
}

static bp::object callobject(bp::object c, bp::object args, bp::object kwds)
{
    return c(*args, **kwds);
}

int Interp::pycall(setup_pointer settings,
		   const char *funcname,
		   double params[])
{
    bp::object retval;
    //    PyGILState_STATE gstate;

    logPy("pycall %s [%f] [%f] this=%lx\n",
	    funcname,params[0],params[1],(unsigned long)this);


    // current_setup = &this->_setup;
    current_setup = get_setup(this); // // &this->_setup;
    current_interp = this;

    if (settings->pymodule_stat != PYMOD_OK) {
	ERS("function '%s.%s' : module not initialized",
	    settings->pymodule,funcname);
	return INTERP_OK;
    }
    // not sure if this is needed
    //    gstate = PyGILState_Ensure();

    try {
	bp::object function = settings->module_namespace[funcname];
	bp::list plist;
	for (int i = 0; i < 30; i++) {
	    plist.append(params[i]);
	}

	retval = callobject(function,bp::make_tuple(plist),
				       settings->kwargs);
    }
    catch (bp::error_already_set) {
	std::string msg = handle_pyerror();
	logPy("pycall: %s",msg.c_str());
	ERS("pycall: %s",msg.c_str());
	PyErr_Clear();
	//	PyGILState_Release(gstate);
	return INTERP_OK;
    }

    if (retval.ptr() != Py_None) {
	if (!PyFloat_Check(retval.ptr())) {
	    PyObject *res_str = PyObject_Str(retval.ptr());
	    ERS("function '%s.%s' returned '%s' - expected float, got %s",
		settings->pymodule,funcname,
		PyString_AsString(res_str),
		retval.ptr()->ob_type->tp_name);
	    Py_XDECREF(res_str);
	    //	    PyGILState_Release(gstate);
	    return INTERP_OK;
	} else {
	    settings->return_value = bp::extract<double>(retval);
	    logPy("pycall: '%s' returned %f", funcname,settings->return_value);
	}
    } else {
	logPy("pycall: '%s' returned no value",funcname);
    }

    //    PyGILState_Release(gstate);
    return INTERP_OK;
}
