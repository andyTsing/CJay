/*
 * CJ.cpp
 *
 *  Created on: May 30, 2014
 *      Author: msn
 */

#define DEFAULT_JNI_VERSION JNI_VERSION_1_8
#define CONSTRUCTOR_METHOD_NAME "<init>"

#include "Handler.h"

namespace VM {

JNIEnv* env = NULL;
JavaVM* jvm = NULL;

// SignatureBase Members
SignatureBase::SignatureBase(std::string descriptor, bool isStatic, const char* rv) :
    descriptor(descriptor), isStatic(isStatic), mid(NULL) {
    switch (*rv)
    {
    case 'Z' : this->rv = RV::Z; break;
    case 'B' : this->rv = RV::B; break;
    case 'C' : this->rv = RV::C; break;
    case 'S' : this->rv = RV::S; break;
    case 'I' : this->rv = RV::I; break;
    case 'J' : this->rv = RV::J; break;
    case 'F' : this->rv = RV::F; break;
    case 'D' : this->rv = RV::D; break;
    case 'L' : this->rv = RV::L; break;
    case 'V' : this->rv = RV::VV; break;
    default :
        throw HandlerExc("Malformed method descriptor. Please review syntax.");
    }
}

SignatureBase::SignatureBase() : descriptor(""), isStatic(true), rv(RV::L), mid(NULL) { }
SignatureBase::~SignatureBase() { }

// CJ Members

CJ::CJ() : clazz(NULL), obj(NULL) {
    this->m.clear();
}

CJ::~CJ() {
    // avoid memory leaks
    for (auto& kv : this->m) {
        delete kv.second;
        kv.second = NULL;
    }
}

void CJ::setSignature(std::string key, std::string descriptor, bool isStatic) {
    SignatureBase* signature;
	std::string rv;

    std::size_t pos = descriptor.find(")");
    rv = descriptor[pos+1];

    signature = new SignatureBase(descriptor, isStatic, rv.c_str());
	this->m.insert(signature_t::value_type(key, signature));
}

void CJ::printSignatures() {
    for (auto& it : m) {
        std::string key = it.first;
        std::cout <<
                "<" <<
                "Method:" << key <<
                ", Descriptor:" << this->m[key]->descriptor <<
                ", isStatic:" << this->m[key]->isStatic <<
                //", RV:" << this->m[key]->rv <<
                ">" <<
                std::endl;
    }
}

jclass CJ::getClass() {
    return this->clazz;
}

jobject CJ::getObj() {
    return this->obj;
}

signature_t CJ::getMap() {
    return this->m;
}

std::string CJ::getDescriptor(std::string key) {
    return this->getSignatureObj(key)->descriptor;
}

jmethodID CJ::getMid(std::string key) {
    return this->getSignatureObj(key)->mid;
}

int CJ::getSizeSignatures() {
    return m.size();
}

void CJ::createVM(std::vector<std::string> vmOption) {
    if (env == NULL || jvm == NULL) {
        int nOptions = vmOption.size();

        JavaVMInitArgs vm_args;
        JavaVMOption* options = new JavaVMOption[nOptions];

        for (std::vector<std::string>::iterator it = vmOption.begin(); it != vmOption.end(); ++it) {
            options[it-vmOption.begin()].optionString = (char*) it->c_str();
        }

        vm_args.version = VM::CJ::JNI_VERSION;
        vm_args.nOptions = nOptions;
        vm_args.options = options;
        vm_args.ignoreUnrecognized = JNI_FALSE;
        int status = JNI_CreateJavaVM(&jvm, (void**)&env, &vm_args); // create only once with global variables
        if(status != JNI_OK) {
            delete options;
            throw HandlerExc("Unable to Launch JVM");
        }

        delete options; // clean memory leaks
    }
}

void CJ::destroyVM() {
    jvm->DestroyJavaVM();
}

void CJ::setClass(std::string className) {
    if (env == NULL || jvm == NULL) {
    	throw HandlerExc("JVM was not instantiated. Please call member createJVM.");
    }

	this->className = className;
    this->clazz = env->FindClass(className.c_str());
    if (this->clazz == NULL) {
        jthrowable exc = env->ExceptionOccurred();
        if (exc) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            throw HandlerExc("JVM: Can't find class: " + className);
        }
    }

    VM::SignatureBase* obj;
    jmethodID mid;
    for (auto& it : this->m) {
        std::string key = it.first;
        obj = it.second;
        // assign values to be updated
        if (obj->isStatic) {
        	mid = env->GetStaticMethodID(this->clazz, key.c_str(), obj->descriptor.c_str());
        } else {
        	mid = env->GetMethodID(this->clazz, key.c_str(), obj->descriptor.c_str());
        }
        if (mid == NULL) {
            jthrowable exc;
            exc = env->ExceptionOccurred();
            if (exc) {
                env->ExceptionDescribe();
                env->ExceptionClear();
                throw HandlerExc(
                        "Failed to get method ID. Please check the syntax of method: " +
                        key +
                        ", which has the descriptor: " +
                        obj->descriptor
                        );
            }
        }
        // update map
        it.second->mid = mid;
    }

}

VM::SignatureBase* CJ::getSignatureObj(std::string key) {
    if ( this->m.find(key) == this->m.end() ) {
        throw HandlerExc("Key does not exit. Use setSignature member beforehand.");
    }
    return this->m[key];
}

void CJ::callClassConstructor_(int mangledVar, ...) {
    // Get Method Id (Constructor)
    VM::SignatureBase* sig = this->getSignatureObj( std::string(CONSTRUCTOR_METHOD_NAME) );
    jmethodID mid = sig->mid;
    jobject obj;

    if(mid == NULL) {
        throw HandlerExc("MethodID not set. Probably set class was not set.");
    }

    va_list args;
    va_start(args, mangledVar);

    obj = env->NewObjectV(this->clazz, mid, args);

    va_end(args);

    this->obj = obj;
}

template <> jboolean CJ::call(std::string methodName, ...) {
    VM::SignatureBase* sig = this->getSignatureObj(methodName);
    jboolean jobj;
    jmethodID mid = sig->mid;
    bool isStatic = sig->isStatic;

    va_list args;
    va_start(args, methodName);

    if (isStatic) {
        jobj = env->CallStaticBooleanMethodV(this->clazz, mid, args);
    } else {
        jobj = env->CallBooleanMethodV(this->obj, mid, args);
    }

    va_end(args);
    return jobj;
}

template <> jbyte CJ::call(std::string methodName, ...) {
    VM::SignatureBase* sig = this->getSignatureObj(methodName);
    jbyte jobj;
    jmethodID mid = sig->mid;
    bool isStatic = sig->isStatic;

    va_list args;
    va_start(args, methodName);

    if (isStatic) {
        jobj = env->CallStaticByteMethodV(this->clazz, mid, args);
    } else {
        jobj = env->CallByteMethodV(this->obj, mid, args);
    }

    va_end(args);
    return jobj;
}

template <> jchar CJ::call(std::string methodName, ...) {
    VM::SignatureBase* sig = this->getSignatureObj(methodName);
    jchar jobj;
    jmethodID mid = sig->mid;
    bool isStatic = sig->isStatic;

    va_list args;
    va_start(args, methodName);

    if (isStatic) {
        jobj = env->CallStaticCharMethodV(this->clazz, mid, args);
    } else {
        jobj = env->CallCharMethodV(this->obj, mid, args);
    }

    va_end(args);
    return jobj;
}

template <> jshort CJ::call(std::string methodName, ...) {
    VM::SignatureBase* sig = this->getSignatureObj(methodName);
    jshort jobj;
    jmethodID mid = sig->mid;
    bool isStatic = sig->isStatic;

    va_list args;
    va_start(args, methodName);

    if (isStatic) {
        jobj = env->CallStaticShortMethodV(this->clazz, mid, args);
    } else {
        jobj = env->CallShortMethodV(this->obj, mid, args);
    }

    va_end(args);
    return jobj;
}

template <> jint CJ::call(std::string methodName, ...) {
    VM::SignatureBase* sig = this->getSignatureObj(methodName);
    jint jobj;
    jmethodID mid = sig->mid;
    bool isStatic = sig->isStatic;

    va_list args;
    va_start(args, methodName);

    if (isStatic) {
        jobj = env->CallStaticIntMethodV(this->clazz, mid, args);
    } else {
        jobj = env->CallIntMethodV(this->obj, mid, args);
    }

    va_end(args);
    return jobj;
}

template <> jlong CJ::call(std::string methodName, ...) {
    VM::SignatureBase* sig = this->getSignatureObj(methodName);
    jlong jobj;
    jmethodID mid = sig->mid;
    bool isStatic = sig->isStatic;

    va_list args;
    va_start(args, methodName);

    if (isStatic) {
        jobj = env->CallStaticLongMethodV(this->clazz, mid, args);
    } else {
        jobj = env->CallLongMethodV(this->obj, mid, args);
    }

    va_end(args);
    return jobj;
}

template <> jfloat CJ::call(std::string methodName, ...) {
    VM::SignatureBase* sig = this->getSignatureObj(methodName);
    jfloat jobj;
    jmethodID mid = sig->mid;
    bool isStatic = sig->isStatic;

    va_list args;
    va_start(args, methodName);

    if (isStatic) {
        jobj = env->CallStaticFloatMethodV(this->clazz, mid, args);
    } else {
        jobj = env->CallFloatMethodV(this->obj, mid, args);
    }

    va_end(args);
    return jobj;
}

template <> jdouble CJ::call(std::string methodName, ...) {
    VM::SignatureBase* sig = this->getSignatureObj(methodName);
    jdouble jobj;
    jmethodID mid = sig->mid;
    bool isStatic = sig->isStatic;

    va_list args;
    va_start(args, methodName);

    if (isStatic) {
        jobj = env->CallStaticDoubleMethodV(this->clazz, mid, args);
    } else {
        jobj = env->CallDoubleMethodV(this->obj, mid, args);
    }

    va_end(args);
    return jobj;
}

template <> jobject CJ::call(std::string methodName, ...) {
    VM::SignatureBase* sig = this->getSignatureObj(methodName);
    jobject jobj;
    jmethodID mid = sig->mid;
    bool isStatic = sig->isStatic;

    va_list args;
    va_start(args, methodName);

    if (isStatic) {
        jobj = env->CallStaticObjectMethodV(this->clazz, mid, args);
    } else {
        jobj = env->CallObjectMethodV(this->obj, mid, args);
    }

    va_end(args);
    return jobj;
}

template <> void CJ::call(std::string methodName, ...) {
    VM::SignatureBase* sig = this->getSignatureObj(methodName);
    jmethodID mid = sig->mid;
    bool isStatic = sig->isStatic;

    va_list args;
    va_start(args, methodName);

    if (isStatic) {
        env->CallStaticFloatMethodV(this->clazz, mid, args);
    } else {
        env->CallFloatMethodV(this->obj, mid, args);
    }

    va_end(args);
}

JNIEnv* CJ::getEnv() {
    return env;
}

// ConverterBase Members (super class)
ConverterBase::ConverterBase() { }

ConverterBase::~ConverterBase() { }

/* Converter Members (child class)
*/
Converter::Converter(): ConverterBase() { this->init(); }
Converter::~Converter() { }

void Converter::initARRAYLIST() {
    ARRAYLIST.setSignature( std::string("toString"), std::string("()Ljava/lang/String;"), false );
    ARRAYLIST.setSignature( std::string("get"), std::string("(I)Ljava/lang/Object;"), false );
    ARRAYLIST.setSignature( std::string("size"), std::string("()I"), false );

    ARRAYLIST.setClass("java/util/ArrayList");
}
void Converter::initMAP() { }

void Converter::initNUMBER() {
    NUMBER.setSignature( std::string("intValue"), std::string("()I"), false );
    NUMBER.setSignature( std::string("longValue"), std::string("()J"), false );
    NUMBER.setSignature( std::string("floatValue"), std::string("()F"), false );
    NUMBER.setSignature( std::string("doubleValue"), std::string("()D"), false );
    NUMBER.setSignature( std::string("shortValue"), std::string("()S"), false );
    NUMBER.setSignature( std::string("byteValue"), std::string("()B"), false );

    NUMBER.setClass("java/lang/Number");
}

void Converter::init() {
    this->initARRAYLIST();
    this->initMAP();
    this->initNUMBER();
}

template <> jbyte Converter::j_cast(int x) {
    // signed 8 bits | min: 128 max: 127
    return (jbyte) x;
}

template <> jshort Converter::j_cast(short x) {
    // signed 16 bits | min: -32,768 max: 32,767
    return (jshort) x;
}

template <> jint Converter::j_cast(int x) {
    // signed 32 bits | min: -2^31 max: 2^31-1
    return (jint) x;
}

template <> jlong Converter::j_cast(long x) {
    // signed 32 bits | min: -2^31 max: 2^31-1
    return (jlong) x;
}

template <> jlong Converter::j_cast(long long x) {
    // signed 32 bits | min: -2^31 max: 2^31-1
    return (jlong) x;
}

template <> jfloat Converter::j_cast(float x) {
    // single-precision 32-bit IEEE 754 floating point
    return (jfloat) x;
}

template <> jdouble Converter::j_cast(double x) {
    // single-precision 64-bit IEEE 754 floating point
    return (jdouble) x;
}

template <> jboolean Converter::j_cast(bool x) {
    return (jboolean) x;
}

template <> jchar Converter::j_cast(char x) {
    // single 16-bit Unicode character.
    // minimum value of '\u0000' (or 0) and a maximum value of '\uffff' (or 65,535 inclusive)
    return (jchar) x;
}

template <> jstring Converter::j_cast(std::string str) {
    return env->NewStringUTF(str.c_str());
}

template <> jstring Converter::j_cast(const char* str) {
    return env->NewStringUTF(str);
}

/*
template <> vec_jobj Converter::c_cast(jobject jobj) {
    vec_jobj v_jobj;
    v_jobj = this->c_cast_vector_obj(jobj);

    return v_jobj;
}
*/

template <> std::string Converter::c_cast(jobject jobj) {
    return std::string(env->GetStringUTFChars((jstring) jobj, JNI_FALSE));
}

int Converter::sizeVector(jobject jobj) {
    VM::SignatureBase* sig = ARRAYLIST.getSignatureObj("size");

    return env->CallIntMethod(jobj, sig->mid, NULL);
}

/*
inline jobject WRAPPER_METHODV(JNIEnv* env, VM::SignatureBase* sig, jclass jclazz, jobject jobj, ...) {
    jobject jresult;

    va_list args;
    va_start(args, jobj);

    jresult = sig->callMethod(env, jclazz, jobj, args);

    va_end(args);

    return jresult;
}
*/
/*
vec_jobj Converter::toVecObject(jobject jobj) {
    VM::SignatureBase* sig = ARRAYLIST.getSignatureObj("get");
    vec_jobj cppVec;
    jobject elem;
    int size = this->szVec(jobj);

    for (int i = 0 ; i < size ; i++) {
        elem = env->CallObjectMethod(jobj, sig->mid, (jint) i);
        cppVec.push_back(elem);
    }

    return cppVec;
}

vec_jobj Converter::c_cast_vector_obj(jobject jobj, int size) {
    VM::SignatureBase* sig = ARRAYLIST.getSignatureObj("get");
    vec_jobj cppVec;
    jobject elem;

    for (int i = 0 ; i < size ; i++) {
        elem = env->CallObjectMethod(jobj, sig->mid, (jint) i);
        cppVec.push_back(elem);
    }

    return cppVec;
}
*/

template <> std::vector<int> Converter::c_cast_vector(jobject jobj, int size) {
    jmethodID midARRAYLIST = ARRAYLIST.getSignatureObj("get")->mid;
    jmethodID midNUMERIC = NUMBER.getSignatureObj("intValue")->mid;
    jobject e;
    std::vector<int> v;

    for (int i = 0 ; i < size ; i++) {
        e = env->CallObjectMethod(jobj, midARRAYLIST, (jint) i); // get element
        v.push_back( env->CallIntMethod(e, midNUMERIC, NULL) ); // convert to primitive
    }

    return v;
}

template <> std::vector<int> Converter::c_cast_vector(jobject jobj) {
    std::vector<int> v;

    int size = this->sizeVector(jobj);
    v = this->c_cast_vector<int>(jobj, size);

    return v;
}

/*
std::string Converter::toString(jobject jobj) {
    return env->GetStringUTFChars((jstring) jobj, 0);
}
*/
void Converter::deleteRef(jobject jobj) {
    env->DeleteLocalRef(jobj);
}

} /* namespace VM */
