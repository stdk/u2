#ifndef API_INTERNAL
#define API_INTERNAL

#ifdef WIN32
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C"
#endif

#define CHECK(statement) do { long ret=(statement); if(ret) return ret; } while(0);

#define API_FUNCTION_0_TO_1(code,name,answer_type)\
EXPORT long name(Reader* reader,answer_type *answer)\
{\
	return reader->send_command(code,answer);\
}

#define API_FUNCTION_1_TO_1(code,name,request_type,answer_type)\
EXPORT long name(Reader* reader,request_type *request,answer_type *answer)\
{\
	return reader->send_command(code,request,answer);\
}

#define API_FUNCTION_1_TO_0(code,name,request_type)\
EXPORT long name(Reader* reader,request_type *request)\
{\
	return reader->send_command(code,request,(uint8_t*)0);\
}

#define API_SIDE_EFFECT(code,name)\
EXPORT long name(Reader* reader)\
{\
	return reader->send_command(code);\
}

#endif