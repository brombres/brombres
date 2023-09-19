#ifndef TEST_H
#define TEST_H


// Test.h

#if defined(__cplusplus)
  #define ROGUE_EXTERN_C extern "C"
  #define BEGIN_ROGUE_EXTERN_C extern "C" {
  #define END_ROGUE_EXTERN_C }
#else
  #define ROGUE_EXTERN_C
  #define BEGIN_ROGUE_EXTERN_C
  #define END_ROGUE_EXTERN_C
#endif

#if defined(__cplusplus) && defined(_WIN32)
  #define ROGUE_COMPOUND(name) name
#else
  #define ROGUE_COMPOUND(name) (name)
#endif

#define ROGUE_GC_AUTO

// Set up C conditional compilation defines
#if defined(__EMSCRIPTEN__)
  #define ROGUE_PLATFORM_WEB 1
  #define ROGUE_PLATFORM_DETERMINED 1
#elif defined(ROGUE_PLATFORM_PLAYDATE)
  #define ROGUE_PLATFORM_DETERMINED 1
#endif

#if !defined(ROGUE_PLATFORM_DETERMINED)
  // Handle Apple's wonky defines which used to ALWAYS be defined as 0 or 1 and
  // are now only defined if the platform is active.

  #if defined(__APPLE__)
    #if defined(TARGET_IPHONE_SIMULATOR)
      #if TARGET_IPHONE_SIMULATOR
        #define ROGUE_PLATFORM_IOS 1
      #endif
    #endif

    #if !defined(ROGUE_PLATFORM_IOS)
      #if defined(TARGET_OS_IPHONE)
        #if TARGET_OS_IPHONE
          #define ROGUE_PLATFORM_IOS 1
        #endif
      #endif
    #endif

    #if !defined(ROGUE_PLATFORM_IOS)
      #define ROGUE_PLATFORM_MACOS 1
    #endif

    #define ROGUE_PLATFORM_DETERMINED 1
  #endif
#endif

#if !defined(ROGUE_PLATFORM_DETERMINED)
  #if defined(_WIN32)
  #  define ROGUE_PLATFORM_WINDOWS 1
  #elif defined(__ANDROID__)
  #  define ROGUE_PLATFORM_ANDROID 1
  #elif defined(__linux__)
  #  define ROGUE_PLATFORM_LINUX 1
  #elif defined(__CYGWIN__)
  #  define ROGUE_PLATFORM_LINUX  1
  #  define ROGUE_PLATFORM_CYGWIN 1
  #else
  #  define ROGUE_PLATFORM_GENERIC 1
  #endif
#endif

#if defined(ROGUE_PLATFORM_WINDOWS)
  #pragma warning(disable: 4297) /* unexpected throw warnings */
  #if !defined(UNICODE)
    #define UNICODE
  #endif
  #include <windows.h>
  #include <signal.h>
#else
  #define ROGUE_PLATFORM_UNIX_COMPATIBLE 1
  #include <stdint.h>
#endif

#include <math.h>
#include <stdlib.h>
#include <string.h>

//------------------------------------------------------------------------------
// Logging
//------------------------------------------------------------------------------
#ifdef __ANDROID__
  #include <android/log.h>
  #define ROGUE_LOG(...)       __android_log_print( ANDROID_LOG_INFO,  "Rogue", __VA_ARGS__ )
  #define ROGUE_LOG_ERROR(...) __android_log_print( ANDROID_LOG_ERROR, "Rogue", __VA_ARGS__ )
#else
  #define ROGUE_LOG(...)       printf( __VA_ARGS__ )
  #define ROGUE_LOG_ERROR(...) printf( __VA_ARGS__ )
#endif

//------------------------------------------------------------------------------
// Primitive Types
//------------------------------------------------------------------------------
#if defined(ROGUE_PLATFORM_WINDOWS)
  typedef double           RogueReal64;
  typedef float            RogueReal32;
  typedef __int64          RogueInt64;
  typedef __int32          RogueInt32;
  typedef __int32          RogueCharacter;
  typedef unsigned __int16 RogueWord;
  typedef unsigned char    RogueByte;
  typedef int              RogueLogical;
  typedef unsigned __int64 RogueUInt64;
  typedef unsigned __int32 RogueUInt32;
#else
  typedef double           RogueReal64;
  typedef float            RogueReal32;
  typedef int64_t          RogueInt64;
  typedef int32_t          RogueInt32;
  typedef int32_t          RogueCharacter;
  typedef uint16_t         RogueWord;
  typedef uint8_t          RogueByte;
  typedef int              RogueLogical;
  typedef uint64_t         RogueUInt64;
  typedef uint32_t         RogueUInt32;
#endif
typedef RogueReal64 RogueReal;
#define ROGUE_TYPE_PRIMITIVE 1
#define ROGUE_TYPE_COMPOUND  2
#define ROGUE_TYPE_ENUM      4
#define ROGUE_TYPE_OBJECT    8
#define ROGUE_TYPE_ASPECT    16
#define ROGUE_TYPE_MUTATING  32

#if !defined(ROGUE_MM_GC_THRESHOLD)
  #define ROGUE_MM_GC_THRESHOLD (32 * 1024 * 1024)
#endif

//------------------------------------------------------------------------------
// Classes
//------------------------------------------------------------------------------
#if !defined(ROGUE_MALLOC)
  #define ROGUE_MALLOC malloc
#endif

#if !defined(ROGUE_FREE)
  #define ROGUE_FREE free
#endif

#if !defined(ROGUE_CREATE_OBJECT)
  #define ROGUE_CREATE_OBJECT( TypeName ) \
      ((TypeName*)Rogue_create_object(&Type##TypeName))
#endif

#if !defined(ROGUE_NEW_OBJECT)
  // Creates an untracked Rogue object
  #define ROGUE_NEW_OBJECT( TypeName ) \
      ((TypeName*)Rogue_new_object(&Type##TypeName))
#endif

#if !defined(ROGUE_SINGLETON)
  #define ROGUE_SINGLETON( TypeName ) \
      ((TypeName*)Rogue_singleton(&Type##TypeName,&TypeName##_singleton))
#endif

#if !defined(ROGUE_SET_SINGLETON)
  #define ROGUE_SET_SINGLETON( TypeName, new_singleton ) \
      Rogue_set_singleton(&Type##TypeName,&TypeName##_singleton,new_singleton)
#endif

#if !defined(ROGUE_RELEASE)
  // Allow 'obj' to be GC'd if there are no other references to it.
  #define ROGUE_RELEASE( obj )  Rogue_release( obj )
#endif

#if !defined(ROGUE_RETAIN)
  // Prevent 'obj' from being GC'd if there are no other references to it.
  #define ROGUE_RETAIN( obj )   Rogue_retain( obj )
#endif

BEGIN_ROGUE_EXTERN_C

typedef struct RogueRuntimeType RogueRuntimeType;
typedef void (*RogueFn_Object)(void*);
typedef void (*RogueFn_Object_Object)(void*,void*);

typedef struct RogueObject RogueObject;
typedef struct RogueString RogueString;

struct RogueRuntimeType
{
  const char*        name;
  RogueString*   name_object;
  RogueInt32     index;
  RogueInt32     id;
  RogueInt32     module_name_index;
  RogueInt32     class_data_index;
  RogueInt64     attributes;
  void**             vtable;
  void**             local_pointer_stack; // Objects + compounds with embedded refs
  RogueInt32     local_pointer_capacity;
  RogueInt32     local_pointer_count;
  RogueInt32     size;
  RogueInt32*    base_type_ids;
  RogueInt32     base_type_count;
  RogueObject*   type_info;
  RogueFn_Object fn_init_object;
  RogueFn_Object fn_init;
  RogueFn_Object fn_gc_trace;
  RogueFn_Object fn_on_cleanup;
  RogueFn_Object_Object fn_on_singleton_change;
};

typedef struct RogueCallFrame
{
  const char* procedure;
  const char* filename;
  RogueInt32 line;
} RogueCallFrame;

typedef struct RogueObjectList
{
  RogueObject** data;
  RogueInt32    count;
  RogueInt32    capacity;
} RogueObjectList;

//------------------------------------------------------------------------------
// Runtime
//------------------------------------------------------------------------------
void  Rogue_configure( int argc, char** argv );  // Call this first
int   Rogue_launch(void);                        // Call this second
void  Rogue_check_gc(void);        // Call this every frame or whenever (unless --gc=auto) to maybe GC
void  Rogue_collect_garbage(void); // Call this to force a GC
void  Rogue_exit( int exit_code ); // Internal use - call exit() directly instead
void  Rogue_request_gc(void);      // Ensures next call to Rogue_check_gc() will result in a GC
int   Rogue_quit(void);            // Call to shut down; prints any pending exception & calls Rogue on_exit functions
void  Rogue_clean_up(void);        // Calls clean_up() on every living object w/on_cleanup() then deletes all objects

void* Rogue_release( void* obj );  // Increase the refcount of the given Rogue object, preventing it from being GC'd
void* Rogue_retain( void* obj );   // Decrease the refcount of the given object

void* Rogue_create_object( void* type );
void  Rogue_destroy_object( void* obj );
void* Rogue_new_object( void* type );
void* Rogue_singleton( void* type, void* singleton_ref );
void  Rogue_set_singleton( void* type, void* singleton_ref, void* new_singleton );

void  Rogue_print_exception(void);

void  Rogue_call_stack_push( const char* procedure, const char* filename, int line );
void  Rogue_call_stack_pop(void);
void  RogueRuntimeType_local_pointer_stack_add( RogueRuntimeType* type, void* local_pointer );
void  RogueObjectList_add( RogueObjectList* list, void* obj );

struct RogueObject;

extern struct RogueObject* Rogue_exception;  // if non-null then an exception is being thrown
extern RogueCallFrame*     Rogue_call_stack;
extern int                 Rogue_call_stack_count;
extern int                 Rogue_call_stack_capacity;
extern int                 Rogue_call_stack_line;

extern int RogueMM_bytes_allocated_since_gc;
extern int RogueMM_gc_request;  // 0:none, !0:requested
extern RogueObjectList RogueMM_objects;
extern RogueObjectList RogueMM_objects_requiring_cleanup;

void*        Rogue_as( void* obj, RogueInt32 recast_type_id );
RogueLogical Rogue_instance_of( void* obj, RogueInt32 ancestor_id );

//------------------------------------------------------------------------------
// Generated
//------------------------------------------------------------------------------
typedef struct RogueRuntimeType RogueRuntimeType;
extern RogueRuntimeType TypeRogueLogical;

extern RogueRuntimeType TypeRogueByte;

extern RogueRuntimeType TypeRogueCharacter;

extern RogueRuntimeType TypeRogueInt32;

extern RogueRuntimeType TypeRogueInt64;

extern RogueRuntimeType TypeRogueReal32;

extern RogueRuntimeType TypeRogueReal64;

extern RogueRuntimeType TypeRogueRogueCNativeProperty;

typedef struct RogueStackTraceFrame RogueStackTraceFrame;
extern RogueRuntimeType TypeRogueStackTraceFrame;

typedef struct RogueOptionalInt32 RogueOptionalInt32;
extern RogueRuntimeType TypeRogueOptionalInt32;

typedef struct RogueConsoleCursor RogueConsoleCursor;
extern RogueRuntimeType TypeRogueConsoleCursor;

typedef struct RogueConsoleEventType RogueConsoleEventType;
extern RogueRuntimeType TypeRogueConsoleEventType;

typedef struct RogueConsoleEvent RogueConsoleEvent;
extern RogueRuntimeType TypeRogueConsoleEvent;

typedef struct RogueRangeUpToLessThanxRogueInt32x RogueRangeUpToLessThanxRogueInt32x;
extern RogueRuntimeType TypeRogueRangeUpToLessThanxRogueInt32x;

typedef struct RogueRangeUpToLessThanIteratorxRogueInt32x RogueRangeUpToLessThanIteratorxRogueInt32x;
extern RogueRuntimeType TypeRogueRangeUpToLessThanIteratorxRogueInt32x;

typedef struct RogueWindowsInputRecord RogueWindowsInputRecord;
extern RogueRuntimeType TypeRogueWindowsInputRecord;

typedef struct RogueUnixConsoleMouseEventType RogueUnixConsoleMouseEventType;
extern RogueRuntimeType TypeRogueUnixConsoleMouseEventType;

typedef struct RogueByteList RogueByteList;
extern RogueRuntimeType TypeRogueByteList;

typedef struct RogueString RogueString;
extern RogueRuntimeType TypeRogueString;

typedef struct RogueOPARENFunctionOPARENCPARENCPAREN RogueOPARENFunctionOPARENCPARENCPAREN;
extern RogueRuntimeType TypeRogueOPARENFunctionOPARENCPARENCPAREN;

typedef struct RogueOPARENFunctionOPARENCPARENCPARENList RogueOPARENFunctionOPARENCPARENCPARENList;
extern RogueRuntimeType TypeRogueOPARENFunctionOPARENCPARENCPARENList;

typedef struct RogueGlobal RogueGlobal;
extern RogueRuntimeType TypeRogueGlobal;

typedef struct RogueObject RogueObject;
extern RogueRuntimeType TypeRogueObject;

typedef struct RogueStackTraceFrameList RogueStackTraceFrameList;
extern RogueRuntimeType TypeRogueStackTraceFrameList;

typedef struct RogueStackTrace RogueStackTrace;
extern RogueRuntimeType TypeRogueStackTrace;

typedef struct RogueException RogueException;
extern RogueRuntimeType TypeRogueException;

typedef struct RogueRoutine RogueRoutine;
extern RogueRuntimeType TypeRogueRoutine;

typedef struct RogueCharacterList RogueCharacterList;
extern RogueRuntimeType TypeRogueCharacterList;

typedef struct RogueStringList RogueStringList;
extern RogueRuntimeType TypeRogueStringList;

typedef struct RogueStringPool RogueStringPool;
extern RogueRuntimeType TypeRogueStringPool;

typedef struct RogueSystem RogueSystem;
extern RogueRuntimeType TypeRogueSystem;

typedef struct RogueConsoleMode RogueConsoleMode;
extern RogueRuntimeType TypeRogueConsoleMode;

typedef struct RogueConsole RogueConsole;
extern RogueRuntimeType TypeRogueConsole;

typedef struct RogueObjectPoolxRogueStringx RogueObjectPoolxRogueStringx;
extern RogueRuntimeType TypeRogueObjectPoolxRogueStringx;

typedef struct RogueConsoleEventTypeList RogueConsoleEventTypeList;
extern RogueRuntimeType TypeRogueConsoleEventTypeList;

typedef struct RogueConsoleErrorPrinter RogueConsoleErrorPrinter;
extern RogueRuntimeType TypeRogueConsoleErrorPrinter;

typedef struct RogueFunction_260 RogueFunction_260;
extern RogueRuntimeType TypeRogueFunction_260;

typedef struct RogueStandardConsoleMode RogueStandardConsoleMode;
extern RogueRuntimeType TypeRogueStandardConsoleMode;

typedef struct RogueFunction_262 RogueFunction_262;
extern RogueRuntimeType TypeRogueFunction_262;

typedef struct RogueConsoleEventList RogueConsoleEventList;
extern RogueRuntimeType TypeRogueConsoleEventList;

typedef struct RogueImmediateConsoleMode RogueImmediateConsoleMode;
extern RogueRuntimeType TypeRogueImmediateConsoleMode;

typedef struct RogueUnixConsoleMouseEventTypeList RogueUnixConsoleMouseEventTypeList;
extern RogueRuntimeType TypeRogueUnixConsoleMouseEventTypeList;

#define ROGUE_STRING_COPY           0
#define ROGUE_STRING_BORROW         1
#define ROGUE_STRING_ADOPT          2
#define ROGUE_STRING_PERMANENT      3
#define ROGUE_STRING_PERMANENT_COPY 4

RogueString* RogueString_create( const char* cstring );
RogueString* RogueString_create_from_utf8( const char* cstring, int byte_count, int usage );
RogueString* RogueString_create_from_ascii256( const char* cstring, int byte_count, int usage );
RogueString* RogueString_create_permanent( const char* cstring );
RogueString* RogueString_create_string_table_entry( const char* cstring );
RogueInt32   RogueString_compute_hash_code( RogueString* THISOBJ, RogueInt32 starting_hash );
const char*      RogueString_to_c_string( RogueString* st );
RogueInt32   RogueString_utf8_character_count( const char* cstring, int byte_count );

extern RogueInt32 Rogue_string_table_count;
#if defined(ROGUE_PLATFORM_LINUX)
  #include <signal.h>
#endif
#if defined(ROGUE_PLATFORM_WINDOWS)
  #include <time.h>
  #include <sys/timeb.h>
#else
  #include <sys/time.h>
  #include <spawn.h>
#endif

extern int    Rogue_argc;
extern char** Rogue_argv;

#ifndef PATH_MAX
  #define PATH_MAX 4096
#endif

extern char **environ;
#if defined(__APPLE__)
  ROGUE_EXTERN_C int _NSGetExecutablePath(char* buf, uint32_t* bufsize);
#endif
#if defined(ROGUE_PLATFORM_WINDOWS)
  #ifndef CONSOLE_READ_NOREMOVE
    #define CONSOLE_READ_NOREMOVE 0x0001
  #endif

  #ifndef CONSOLE_READ_NOWAIT
    #define CONSOLE_READ_NOWAIT   0x0002
  #endif
#endif
#if defined(ROGUE_PLATFORM_WINDOWS)
  #include <io.h>
  #define ROGUE_READ_CALL _read
#elif !defined(ROGUE_PLATFORM_EMBEDDED)
  #include <fcntl.h>
  #include <termios.h>
  #include <unistd.h>
  #include <sys/ioctl.h>
  #define ROGUE_READ_CALL read
#endif

#ifndef STDIN_FILENO      /* Probably Windows */
  #define STDIN_FILENO  0 /* Probably correct */
  #define STDOUT_FILENO 1
  #define STDERR_FILENO 2
#endif

void Rogue_fwrite( const char* utf8, int byte_count, int out );
struct RogueStackTraceFrame
{
  RogueString* procedure;
  RogueString* filename;
  RogueInt32 line;
};

void RogueStackTraceFrame_gc_trace( void* THISOBJ );

struct RogueOptionalInt32
{
  RogueInt32 value;
  RogueLogical exists;
};

struct RogueConsoleCursor
{
  int dummy;
};

struct RogueConsoleEventType
{
  RogueInt32 value;
};

struct RogueConsoleEvent
{
  RogueConsoleEventType type;
  RogueInt32 x;
  RogueInt32 y;
};

struct RogueRangeUpToLessThanxRogueInt32x
{
  RogueInt32 start;
  RogueInt32 limit;
  RogueInt32 step;
};

struct RogueRangeUpToLessThanIteratorxRogueInt32x
{
  RogueInt32 cur;
  RogueInt32 limit;
  RogueInt32 step;
};

struct RogueWindowsInputRecord
{
  #if defined(ROGUE_PLATFORM_WINDOWS)
  INPUT_RECORD value;
  #else
  int dummy;
  #endif
};

struct RogueUnixConsoleMouseEventType
{
  RogueInt32 value;
};

struct RogueByteList
{
  RogueRuntimeType* __type;
  RogueInt32 __refcount;
  RogueInt32 count;
  RogueInt32 capacity;
  RogueInt32 element_size;
  RogueLogical is_ref_array;
  RogueLogical is_borrowed;
  RogueByte element_type;
  union
  {
    void*           data;
    void*           as_void;
    RogueObject**   as_objects;
    RogueString**   as_strings;
    RogueReal64*    as_real64s;
    RogueReal32*    as_real32s;
    RogueInt64*     as_int64s;
    RogueInt32*     as_int32s;
    RogueCharacter* as_characters;
    RogueWord*      as_words;
    RogueByte*      as_bytes;
    char*               as_utf8; // only valid for String data; includes null terminator
    RogueByte*      as_logicals;
    #if defined(ROGUE_PLATFORM_WINDOWS)
      wchar_t*          as_wchars;
    #endif
  };
};

void RogueByteList_gc_trace( void* THISOBJ );

struct RogueString
{
  RogueRuntimeType* __type;
  RogueInt32 __refcount;
  RogueInt32 count;
  RogueInt32 cursor_offset;
  RogueInt32 cursor_index;
  RogueInt32 hash_code;
  RogueInt32 indent;
  RogueLogical at_newline;
  RogueLogical is_immutable;
  RogueLogical is_ascii;
  RogueByteList* data;
};

void RogueString_gc_trace( void* THISOBJ );

struct RogueOPARENFunctionOPARENCPARENCPAREN
{
  RogueRuntimeType* __type;
  RogueInt32 __refcount;
};

void RogueOPARENFunctionOPARENCPARENCPAREN_gc_trace( void* THISOBJ );

struct RogueOPARENFunctionOPARENCPARENCPARENList
{
  RogueRuntimeType* __type;
  RogueInt32 __refcount;
  RogueInt32 count;
  RogueInt32 capacity;
  RogueInt32 element_size;
  RogueLogical is_ref_array;
  RogueLogical is_borrowed;
  RogueOPARENFunctionOPARENCPARENCPAREN* element_type;
  union
  {
    void*           data;
    void*           as_void;
    RogueObject**   as_objects;
    RogueString**   as_strings;
    RogueReal64*    as_real64s;
    RogueReal32*    as_real32s;
    RogueInt64*     as_int64s;
    RogueInt32*     as_int32s;
    RogueCharacter* as_characters;
    RogueWord*      as_words;
    RogueByte*      as_bytes;
    char*               as_utf8; // only valid for String data; includes null terminator
    RogueByte*      as_logicals;
    #if defined(ROGUE_PLATFORM_WINDOWS)
      wchar_t*          as_wchars;
    #endif
  };
};

void RogueOPARENFunctionOPARENCPARENCPARENList_gc_trace( void* THISOBJ );

struct RogueGlobal
{
  RogueRuntimeType* __type;
  RogueInt32 __refcount;
  RogueString* global_output_buffer;
  RogueObject* output;
  RogueObject* error;
  RogueObject* log;
  RogueOPARENFunctionOPARENCPARENCPARENList* exit_functions;
};

void RogueGlobal_gc_trace( void* THISOBJ );

struct RogueObject
{
  RogueRuntimeType* __type;
  RogueInt32 __refcount;
};

void RogueObject_gc_trace( void* THISOBJ );

struct RogueStackTraceFrameList
{
  RogueRuntimeType* __type;
  RogueInt32 __refcount;
  RogueInt32 count;
  RogueInt32 capacity;
  RogueInt32 element_size;
  RogueLogical is_ref_array;
  RogueLogical is_borrowed;
  RogueStackTraceFrame element_type;
  union
  {
    void*           data;
    void*           as_void;
    RogueObject**   as_objects;
    RogueString**   as_strings;
    RogueReal64*    as_real64s;
    RogueReal32*    as_real32s;
    RogueInt64*     as_int64s;
    RogueInt32*     as_int32s;
    RogueCharacter* as_characters;
    RogueWord*      as_words;
    RogueByte*      as_bytes;
    char*               as_utf8; // only valid for String data; includes null terminator
    RogueByte*      as_logicals;
    #if defined(ROGUE_PLATFORM_WINDOWS)
      wchar_t*          as_wchars;
    #endif
  };
};

void RogueStackTraceFrameList_gc_trace( void* THISOBJ );

struct RogueStackTrace
{
  RogueRuntimeType* __type;
  RogueInt32 __refcount;
  RogueStackTraceFrameList* frames;
};

void RogueStackTrace_gc_trace( void* THISOBJ );

struct RogueException
{
  RogueRuntimeType* __type;
  RogueInt32 __refcount;
  RogueString* message;
  RogueStackTrace* stack_trace;
};

void RogueException_gc_trace( void* THISOBJ );

struct RogueRoutine
{
  RogueRuntimeType* __type;
  RogueInt32 __refcount;
};

void RogueRoutine_gc_trace( void* THISOBJ );

struct RogueCharacterList
{
  RogueRuntimeType* __type;
  RogueInt32 __refcount;
  RogueInt32 count;
  RogueInt32 capacity;
  RogueInt32 element_size;
  RogueLogical is_ref_array;
  RogueLogical is_borrowed;
  RogueCharacter element_type;
  union
  {
    void*           data;
    void*           as_void;
    RogueObject**   as_objects;
    RogueString**   as_strings;
    RogueReal64*    as_real64s;
    RogueReal32*    as_real32s;
    RogueInt64*     as_int64s;
    RogueInt32*     as_int32s;
    RogueCharacter* as_characters;
    RogueWord*      as_words;
    RogueByte*      as_bytes;
    char*               as_utf8; // only valid for String data; includes null terminator
    RogueByte*      as_logicals;
    #if defined(ROGUE_PLATFORM_WINDOWS)
      wchar_t*          as_wchars;
    #endif
  };
};

void RogueCharacterList_gc_trace( void* THISOBJ );

struct RogueStringList
{
  RogueRuntimeType* __type;
  RogueInt32 __refcount;
  RogueInt32 count;
  RogueInt32 capacity;
  RogueInt32 element_size;
  RogueLogical is_ref_array;
  RogueLogical is_borrowed;
  RogueString* element_type;
  union
  {
    void*           data;
    void*           as_void;
    RogueObject**   as_objects;
    RogueString**   as_strings;
    RogueReal64*    as_real64s;
    RogueReal32*    as_real32s;
    RogueInt64*     as_int64s;
    RogueInt32*     as_int32s;
    RogueCharacter* as_characters;
    RogueWord*      as_words;
    RogueByte*      as_bytes;
    char*               as_utf8; // only valid for String data; includes null terminator
    RogueByte*      as_logicals;
    #if defined(ROGUE_PLATFORM_WINDOWS)
      wchar_t*          as_wchars;
    #endif
  };
};

void RogueStringList_gc_trace( void* THISOBJ );

struct RogueStringPool
{
  RogueRuntimeType* __type;
  RogueInt32 __refcount;
  RogueStringList* available;
};

void RogueStringPool_gc_trace( void* THISOBJ );

struct RogueSystem
{
  RogueRuntimeType* __type;
  RogueInt32 __refcount;
};

void RogueSystem_gc_trace( void* THISOBJ );

struct RogueConsoleMode
{
  RogueRuntimeType* __type;
  RogueInt32 __refcount;
};

void RogueConsoleMode_gc_trace( void* THISOBJ );

struct RogueConsole
{
  RogueRuntimeType* __type;
  RogueInt32 __refcount;
  RogueInt32 position;
  RogueString* output_buffer;
  RogueObject* error;
  RogueConsoleCursor cursor;
  RogueLogical is_end_of_input;
  RogueLogical immediate_mode;
  RogueLogical decode_utf8;
  RogueConsoleMode* mode;
  RogueLogical windows_in_quick_edit_mode;
  RogueByteList* input_buffer;
  RogueOptionalInt32 next_input_character;
  RogueByteList* _input_bytes;
  RogueLogical force_input_blocking;
  #if !defined(ROGUE_PLATFORM_WINDOWS) && !defined(ROGUE_PLATFORM_EMBEDDED)
    struct termios original_terminal_settings;
    int            original_stdin_flags;
  #endif
};

void RogueConsole_gc_trace( void* THISOBJ );

struct RogueObjectPoolxRogueStringx
{
  RogueRuntimeType* __type;
  RogueInt32 __refcount;
  RogueStringList* available;
};

void RogueObjectPoolxRogueStringx_gc_trace( void* THISOBJ );

struct RogueConsoleEventTypeList
{
  RogueRuntimeType* __type;
  RogueInt32 __refcount;
  RogueInt32 count;
  RogueInt32 capacity;
  RogueInt32 element_size;
  RogueLogical is_ref_array;
  RogueLogical is_borrowed;
  RogueConsoleEventType element_type;
  union
  {
    void*           data;
    void*           as_void;
    RogueObject**   as_objects;
    RogueString**   as_strings;
    RogueReal64*    as_real64s;
    RogueReal32*    as_real32s;
    RogueInt64*     as_int64s;
    RogueInt32*     as_int32s;
    RogueCharacter* as_characters;
    RogueWord*      as_words;
    RogueByte*      as_bytes;
    char*               as_utf8; // only valid for String data; includes null terminator
    RogueByte*      as_logicals;
    #if defined(ROGUE_PLATFORM_WINDOWS)
      wchar_t*          as_wchars;
    #endif
  };
};

void RogueConsoleEventTypeList_gc_trace( void* THISOBJ );

struct RogueConsoleErrorPrinter
{
  RogueRuntimeType* __type;
  RogueInt32 __refcount;
  RogueString* output_buffer;
};

void RogueConsoleErrorPrinter_gc_trace( void* THISOBJ );

struct RogueFunction_260
{
  RogueRuntimeType* __type;
  RogueInt32 __refcount;
};

void RogueFunction_260_gc_trace( void* THISOBJ );

struct RogueStandardConsoleMode
{
  RogueRuntimeType* __type;
  RogueInt32 __refcount;
  RogueOptionalInt32 next_input_character;
};

void RogueStandardConsoleMode_gc_trace( void* THISOBJ );

struct RogueFunction_262
{
  RogueRuntimeType* __type;
  RogueInt32 __refcount;
};

void RogueFunction_262_gc_trace( void* THISOBJ );

struct RogueConsoleEventList
{
  RogueRuntimeType* __type;
  RogueInt32 __refcount;
  RogueInt32 count;
  RogueInt32 capacity;
  RogueInt32 element_size;
  RogueLogical is_ref_array;
  RogueLogical is_borrowed;
  RogueConsoleEvent element_type;
  union
  {
    void*           data;
    void*           as_void;
    RogueObject**   as_objects;
    RogueString**   as_strings;
    RogueReal64*    as_real64s;
    RogueReal32*    as_real32s;
    RogueInt64*     as_int64s;
    RogueInt32*     as_int32s;
    RogueCharacter* as_characters;
    RogueWord*      as_words;
    RogueByte*      as_bytes;
    char*               as_utf8; // only valid for String data; includes null terminator
    RogueByte*      as_logicals;
    #if defined(ROGUE_PLATFORM_WINDOWS)
      wchar_t*          as_wchars;
    #endif
  };
};

void RogueConsoleEventList_gc_trace( void* THISOBJ );

struct RogueImmediateConsoleMode
{
  RogueRuntimeType* __type;
  RogueInt32 __refcount;
  RogueConsoleEventList* events;
  RogueLogical decode_utf8;
  RogueInt32 windows_button_state;
  RogueConsoleEventType windows_last_press_type;
};

void RogueImmediateConsoleMode_gc_trace( void* THISOBJ );

struct RogueUnixConsoleMouseEventTypeList
{
  RogueRuntimeType* __type;
  RogueInt32 __refcount;
  RogueInt32 count;
  RogueInt32 capacity;
  RogueInt32 element_size;
  RogueLogical is_ref_array;
  RogueLogical is_borrowed;
  RogueUnixConsoleMouseEventType element_type;
  union
  {
    void*           data;
    void*           as_void;
    RogueObject**   as_objects;
    RogueString**   as_strings;
    RogueReal64*    as_real64s;
    RogueReal32*    as_real32s;
    RogueInt64*     as_int64s;
    RogueInt32*     as_int32s;
    RogueCharacter* as_characters;
    RogueWord*      as_words;
    RogueByte*      as_bytes;
    char*               as_utf8; // only valid for String data; includes null terminator
    RogueByte*      as_logicals;
    #if defined(ROGUE_PLATFORM_WINDOWS)
      wchar_t*          as_wchars;
    #endif
  };
};

void RogueUnixConsoleMouseEventTypeList_gc_trace( void* THISOBJ );

RogueInt32 RogueInt32__abs( RogueInt32 THISOBJ );
RogueInt32 RogueInt32__clamped_low__RogueInt32( RogueInt32 THISOBJ, RogueInt32 low_0 );
RogueInt32 RogueInt32__digit_count( RogueInt32 THISOBJ );
RogueInt32 RogueInt32__or_larger__RogueInt32( RogueInt32 THISOBJ, RogueInt32 other_0 );
RogueInt64 RogueInt64__operatorMOD__RogueInt64( RogueInt64 THISOBJ, RogueInt64 other_0 );
RogueString* RogueStackTraceFrame__toxRogueStringx( RogueStackTraceFrame THISOBJ );
RogueString* RogueStackTraceFrame__toxRogueStringx__RogueInt32_RogueInt32( RogueStackTraceFrame THISOBJ, RogueInt32 left_w_0, RogueInt32 right_w_1 );
extern RogueLogical RogueConsoleCursor__g_cursor_hidden;
void RogueConsoleCursor__init_class(void);
void RogueConsoleCursor__hide__RogueLogical_RoguePrintWriter( RogueConsoleCursor THISOBJ, RogueLogical setting_0, RogueObject* output_1 );
void RogueConsoleCursor__show__RoguePrintWriter( RogueConsoleCursor THISOBJ, RogueObject* output_0 );
extern RogueConsoleEventTypeList* RogueConsoleEventType__g_categories;
void RogueConsoleEventType__init_class(void);
RogueLogical RogueConsoleEventType__operator____RogueConsoleEventType( RogueConsoleEventType THISOBJ, RogueConsoleEventType other_0 );
RogueString* RogueConsoleEventType__toxRogueStringx( RogueConsoleEventType THISOBJ );
RogueLogical RogueConsoleEvent__is_character( RogueConsoleEvent THISOBJ );
RogueString* RogueConsoleEvent__toxRogueStringx( RogueConsoleEvent THISOBJ );
RogueRangeUpToLessThanIteratorxRogueInt32x RogueRangeUpToLessThanxRogueInt32x__iterator( RogueRangeUpToLessThanxRogueInt32x THISOBJ );
RogueOptionalInt32 RogueRangeUpToLessThanIteratorxRogueInt32x__read_another( RogueRangeUpToLessThanIteratorxRogueInt32x* THISOBJ );
extern RogueUnixConsoleMouseEventTypeList* RogueUnixConsoleMouseEventType__g_categories;
void RogueUnixConsoleMouseEventType__init_class(void);
RogueString* RogueUnixConsoleMouseEventType__toxRogueStringx( RogueUnixConsoleMouseEventType THISOBJ );
void RogueByteList__init( RogueByteList* THISOBJ );
void RogueByteList__init__RogueInt32( RogueByteList* THISOBJ, RogueInt32 capacity_0 );
void RogueByteList__init_object( RogueByteList* THISOBJ );
void RogueByteList__on_cleanup( RogueByteList* THISOBJ );
void RogueByteList__add__RogueByte( RogueByteList* THISOBJ, RogueByte value_0 );
void RogueByteList__clear( RogueByteList* THISOBJ );
void RogueByteList__copy__RogueInt32_RogueInt32_RogueByteList_RogueInt32( RogueByteList* THISOBJ, RogueInt32 src_i1_0, RogueInt32 src_count_1, RogueByteList* dest_2, RogueInt32 dest_i1_3 );
RogueString* RogueByteList__description( RogueByteList* THISOBJ );
void RogueByteList__discard_from__RogueInt32( RogueByteList* THISOBJ, RogueInt32 index_0 );
void RogueByteList__ensure_capacity__RogueInt32( RogueByteList* THISOBJ, RogueInt32 desired_capacity_0 );
void RogueByteList__expand_to_count__RogueInt32( RogueByteList* THISOBJ, RogueInt32 minimum_count_0 );
RogueByte RogueByteList__first( RogueByteList* THISOBJ );
RogueByte RogueByteList__get__RogueInt32( RogueByteList* THISOBJ, RogueInt32 index_0 );
void RogueByteList__on_return_to_pool( RogueByteList* THISOBJ );
RogueByte RogueByteList__remove_first( RogueByteList* THISOBJ );
void RogueByteList__reserve__RogueInt32( RogueByteList* THISOBJ, RogueInt32 additional_capacity_0 );
void RogueByteList__set__RogueInt32_RogueByte( RogueByteList* THISOBJ, RogueInt32 index_0, RogueByte value_1 );
void RogueByteList__shift__RogueInt32( RogueByteList* THISOBJ, RogueInt32 delta_0 );
RogueString* RogueByteList__toxRogueStringx( RogueByteList* THISOBJ );
void RogueByteList__zero__RogueInt32_RogueOptionalInt32( RogueByteList* THISOBJ, RogueInt32 i1_0, RogueOptionalInt32 count_1 );
RogueString* RogueByteList__type_name( RogueByteList* THISOBJ );
RogueLogical RogueString__operator____RogueString_RogueString(RogueString* a_0, RogueString* b_1);
RogueString* RogueString__operatorPLUS__RogueString_RogueCharacter(RogueString* left_0, RogueCharacter right_1);
RogueString* RogueString__operatorPLUS__RogueString_RogueString(RogueString* left_0, RogueString* right_1);
void RogueString__init( RogueString* THISOBJ );
void RogueString__init__RogueInt32( RogueString* THISOBJ, RogueInt32 byte_capacity_0 );
void RogueString__clear( RogueString* THISOBJ );
RogueInt32 RogueString__count( RogueString* THISOBJ );
void RogueString__flush( RogueString* THISOBJ );
RogueString* RogueString__from__RogueInt32_RogueInt32( RogueString* THISOBJ, RogueInt32 i1_0, RogueInt32 i2_1 );
RogueCharacter RogueString__get__RogueInt32( RogueString* THISOBJ, RogueInt32 index_0 );
RogueInt32 RogueString__hash_code( RogueString* THISOBJ );
RogueString* RogueString__justified__RogueInt32_RogueCharacter( RogueString* THISOBJ, RogueInt32 spaces_0, RogueCharacter fill_1 );
RogueString* RogueString__leftmost__RogueInt32( RogueString* THISOBJ, RogueInt32 n_0 );
void RogueString__on_return_to_pool( RogueString* THISOBJ );
void RogueString__print__RogueByte( RogueString* THISOBJ, RogueByte value_0 );
void RogueString__print__RogueCharacter( RogueString* THISOBJ, RogueCharacter value_0 );
void RogueString__print__RogueInt32( RogueString* THISOBJ, RogueInt32 value_0 );
void RogueString__print__RogueInt64( RogueString* THISOBJ, RogueInt64 value_0 );
void RogueString__print__RogueObject( RogueString* THISOBJ, RogueObject* value_0 );
void RogueString__print__RogueString( RogueString* THISOBJ, RogueString* value_0 );
void RogueString__println__RogueObject( RogueString* THISOBJ, RogueObject* value_0 );
void RogueString__println__RogueString( RogueString* THISOBJ, RogueString* value_0 );
void RogueString__reserve__RogueInt32( RogueString* THISOBJ, RogueInt32 additional_bytes_0 );
RogueInt32 RogueString__set_cursor__RogueInt32( RogueString* THISOBJ, RogueInt32 character_index_0 );
RogueString* RogueString__toxRogueStringx( RogueString* THISOBJ );
void RogueString__init_object( RogueString* THISOBJ );
RogueString* RogueString__type_name( RogueString* THISOBJ );
void RogueOPARENFunctionOPARENCPARENCPAREN__init_object( RogueOPARENFunctionOPARENCPARENCPAREN* THISOBJ );
RogueString* RogueOPARENFunctionOPARENCPARENCPAREN__type_name( RogueOPARENFunctionOPARENCPARENCPAREN* THISOBJ );
void RogueOPARENFunctionOPARENCPARENCPARENList__init( RogueOPARENFunctionOPARENCPARENCPARENList* THISOBJ );
void RogueOPARENFunctionOPARENCPARENCPARENList__init_object( RogueOPARENFunctionOPARENCPARENCPARENList* THISOBJ );
void RogueOPARENFunctionOPARENCPARENCPARENList__on_cleanup( RogueOPARENFunctionOPARENCPARENCPARENList* THISOBJ );
void RogueOPARENFunctionOPARENCPARENCPARENList__add__RogueOPARENFunctionOPARENCPARENCPAREN( RogueOPARENFunctionOPARENCPARENCPARENList* THISOBJ, RogueOPARENFunctionOPARENCPARENCPAREN* value_0 );
void RogueOPARENFunctionOPARENCPARENCPARENList__clear( RogueOPARENFunctionOPARENCPARENCPARENList* THISOBJ );
RogueString* RogueOPARENFunctionOPARENCPARENCPARENList__description( RogueOPARENFunctionOPARENCPARENCPARENList* THISOBJ );
void RogueOPARENFunctionOPARENCPARENCPARENList__discard_from__RogueInt32( RogueOPARENFunctionOPARENCPARENCPARENList* THISOBJ, RogueInt32 index_0 );
RogueOPARENFunctionOPARENCPARENCPAREN* RogueOPARENFunctionOPARENCPARENCPARENList__get__RogueInt32( RogueOPARENFunctionOPARENCPARENCPARENList* THISOBJ, RogueInt32 index_0 );
void RogueOPARENFunctionOPARENCPARENCPARENList__on_return_to_pool( RogueOPARENFunctionOPARENCPARENCPARENList* THISOBJ );
void RogueOPARENFunctionOPARENCPARENCPARENList__reserve__RogueInt32( RogueOPARENFunctionOPARENCPARENCPARENList* THISOBJ, RogueInt32 additional_capacity_0 );
RogueString* RogueOPARENFunctionOPARENCPARENCPARENList__toxRogueStringx( RogueOPARENFunctionOPARENCPARENCPARENList* THISOBJ );
void RogueOPARENFunctionOPARENCPARENCPARENList__zero__RogueInt32_RogueOptionalInt32( RogueOPARENFunctionOPARENCPARENCPARENList* THISOBJ, RogueInt32 i1_0, RogueOptionalInt32 count_1 );
RogueString* RogueOPARENFunctionOPARENCPARENCPARENList__type_name( RogueOPARENFunctionOPARENCPARENCPARENList* THISOBJ );
void RogueGlobal__call_exit_functions(void);
void RogueGlobal__on_control_c__RogueInt32(RogueInt32 signum_0);
void RogueGlobal__on_segmentation_fault__RogueInt32(RogueInt32 signum_0);
extern RogueGlobal* RogueGlobal_singleton;

void RogueGlobal__init( RogueGlobal* THISOBJ );
void RogueGlobal__configure_standard_output( RogueGlobal* THISOBJ );
void RogueGlobal__flush__RogueString( RogueGlobal* THISOBJ, RogueString* buffer_0 );
void RogueGlobal__on_exit__RogueOPARENFunctionOPARENCPARENCPAREN( RogueGlobal* THISOBJ, RogueOPARENFunctionOPARENCPARENCPAREN* fn_0 );
void RogueGlobal__init_object( RogueGlobal* THISOBJ );
RogueString* RogueGlobal__type_name( RogueGlobal* THISOBJ );
void RogueGlobal__flush( RogueGlobal* THISOBJ );
void RogueGlobal__print__RogueCharacter( RogueGlobal* THISOBJ, RogueCharacter value_0 );
void RogueGlobal__print__RogueObject( RogueGlobal* THISOBJ, RogueObject* value_0 );
void RogueGlobal__print__RogueString( RogueGlobal* THISOBJ, RogueString* value_0 );
void RogueGlobal__println( RogueGlobal* THISOBJ );
void RogueGlobal__println__RogueObject( RogueGlobal* THISOBJ, RogueObject* value_0 );
void RogueGlobal__println__RogueString( RogueGlobal* THISOBJ, RogueString* value_0 );
RogueString* RogueObject____type_name__RogueInt32(RogueInt32 type_index_0);
RogueString* RogueObject__toxRogueStringx( RogueObject* THISOBJ );
void RogueObject__init_object( RogueObject* THISOBJ );
RogueString* RogueObject__type_name( RogueObject* THISOBJ );
void RogueStackTraceFrameList__init__RogueInt32( RogueStackTraceFrameList* THISOBJ, RogueInt32 capacity_0 );
void RogueStackTraceFrameList__init_object( RogueStackTraceFrameList* THISOBJ );
void RogueStackTraceFrameList__on_cleanup( RogueStackTraceFrameList* THISOBJ );
void RogueStackTraceFrameList__add__RogueStackTraceFrame( RogueStackTraceFrameList* THISOBJ, RogueStackTraceFrame value_0 );
void RogueStackTraceFrameList__clear( RogueStackTraceFrameList* THISOBJ );
RogueString* RogueStackTraceFrameList__description( RogueStackTraceFrameList* THISOBJ );
void RogueStackTraceFrameList__discard_from__RogueInt32( RogueStackTraceFrameList* THISOBJ, RogueInt32 index_0 );
RogueStackTraceFrame RogueStackTraceFrameList__get__RogueInt32( RogueStackTraceFrameList* THISOBJ, RogueInt32 index_0 );
RogueStackTraceFrame RogueStackTraceFrameList__last( RogueStackTraceFrameList* THISOBJ );
void RogueStackTraceFrameList__on_return_to_pool( RogueStackTraceFrameList* THISOBJ );
RogueStackTraceFrame RogueStackTraceFrameList__remove_last( RogueStackTraceFrameList* THISOBJ );
void RogueStackTraceFrameList__reserve__RogueInt32( RogueStackTraceFrameList* THISOBJ, RogueInt32 additional_capacity_0 );
RogueString* RogueStackTraceFrameList__toxRogueStringx( RogueStackTraceFrameList* THISOBJ );
void RogueStackTraceFrameList__zero__RogueInt32_RogueOptionalInt32( RogueStackTraceFrameList* THISOBJ, RogueInt32 i1_0, RogueOptionalInt32 count_1 );
RogueString* RogueStackTraceFrameList__type_name( RogueStackTraceFrameList* THISOBJ );
void RogueStackTrace__init( RogueStackTrace* THISOBJ );
RogueString* RogueStackTrace__filename__RogueInt32( RogueStackTrace* THISOBJ, RogueInt32 index_0 );
RogueInt32 RogueStackTrace__line__RogueInt32( RogueStackTrace* THISOBJ, RogueInt32 index_0 );
RogueString* RogueStackTrace__procedure__RogueInt32( RogueStackTrace* THISOBJ, RogueInt32 index_0 );
RogueString* RogueStackTrace__toxRogueStringx( RogueStackTrace* THISOBJ );
void RogueStackTrace__init_object( RogueStackTrace* THISOBJ );
RogueString* RogueStackTrace__type_name( RogueStackTrace* THISOBJ );
void RogueException__display__RogueException(RogueException* err_0);
void RogueException__init_object( RogueException* THISOBJ );
void RogueException__display( RogueException* THISOBJ );
RogueString* RogueException__toxRogueStringx( RogueException* THISOBJ );
RogueString* RogueException__type_name( RogueException* THISOBJ );
void RogueRoutine__on_launch(void);
void RogueRoutine__init_object( RogueRoutine* THISOBJ );
RogueString* RogueRoutine__type_name( RogueRoutine* THISOBJ );
void RogueCharacterList__init( RogueCharacterList* THISOBJ );
void RogueCharacterList__init_object( RogueCharacterList* THISOBJ );
void RogueCharacterList__on_cleanup( RogueCharacterList* THISOBJ );
void RogueCharacterList__add__RogueCharacter( RogueCharacterList* THISOBJ, RogueCharacter value_0 );
void RogueCharacterList__clear( RogueCharacterList* THISOBJ );
RogueString* RogueCharacterList__description( RogueCharacterList* THISOBJ );
void RogueCharacterList__discard_from__RogueInt32( RogueCharacterList* THISOBJ, RogueInt32 index_0 );
RogueCharacter RogueCharacterList__get__RogueInt32( RogueCharacterList* THISOBJ, RogueInt32 index_0 );
void RogueCharacterList__on_return_to_pool( RogueCharacterList* THISOBJ );
void RogueCharacterList__reserve__RogueInt32( RogueCharacterList* THISOBJ, RogueInt32 additional_capacity_0 );
RogueString* RogueCharacterList__toxRogueStringx( RogueCharacterList* THISOBJ );
void RogueCharacterList__zero__RogueInt32_RogueOptionalInt32( RogueCharacterList* THISOBJ, RogueInt32 i1_0, RogueOptionalInt32 count_1 );
RogueString* RogueCharacterList__type_name( RogueCharacterList* THISOBJ );
void RogueStringList__init( RogueStringList* THISOBJ );
void RogueStringList__init_object( RogueStringList* THISOBJ );
void RogueStringList__on_cleanup( RogueStringList* THISOBJ );
void RogueStringList__add__RogueString( RogueStringList* THISOBJ, RogueString* value_0 );
void RogueStringList__clear( RogueStringList* THISOBJ );
RogueString* RogueStringList__description( RogueStringList* THISOBJ );
void RogueStringList__discard_from__RogueInt32( RogueStringList* THISOBJ, RogueInt32 index_0 );
RogueString* RogueStringList__get__RogueInt32( RogueStringList* THISOBJ, RogueInt32 index_0 );
RogueLogical RogueStringList__is_empty( RogueStringList* THISOBJ );
void RogueStringList__on_return_to_pool( RogueStringList* THISOBJ );
RogueString* RogueStringList__remove_last( RogueStringList* THISOBJ );
void RogueStringList__reserve__RogueInt32( RogueStringList* THISOBJ, RogueInt32 additional_capacity_0 );
RogueString* RogueStringList__toxRogueStringx( RogueStringList* THISOBJ );
void RogueStringList__zero__RogueInt32_RogueOptionalInt32( RogueStringList* THISOBJ, RogueInt32 i1_0, RogueOptionalInt32 count_1 );
RogueString* RogueStringList__type_name( RogueStringList* THISOBJ );
extern RogueStringPool* RogueStringPool_singleton;

void RogueStringPool__init_object( RogueStringPool* THISOBJ );
RogueString* RogueStringPool__type_name( RogueStringPool* THISOBJ );
extern RogueStringList* RogueSystem__g_command_line_arguments;
extern RogueString* RogueSystem__g_executable_filepath;
extern RogueInt64 RogueSystem__g_execution_start_ms;
void RogueSystem__exit__RogueInt32(RogueInt32 result_code_0);
RogueLogical RogueSystem__is_windows(void);
RogueInt64 RogueSystem__time_ms(void);
void RogueSystem___add_command_line_argument__RogueString(RogueString* arg_0);
void RogueSystem__init_class(void);
void RogueSystem__init_object( RogueSystem* THISOBJ );
RogueString* RogueSystem__type_name( RogueSystem* THISOBJ );
extern RogueLogical RogueConsoleMode__g_configured_on_exit;
void RogueConsoleMode__init_class(void);
void RogueConsoleMode__init_object( RogueConsoleMode* THISOBJ );
void RogueConsoleMode___on_enter( RogueConsoleMode* THISOBJ );
void RogueConsoleMode___on_exit( RogueConsoleMode* THISOBJ );
RogueString* RogueConsoleMode__type_name( RogueConsoleMode* THISOBJ );
extern RogueConsole* RogueConsole_singleton;

void RogueConsole__init( RogueConsole* THISOBJ );
RogueObject* RogueConsole__error( RogueConsole* THISOBJ );
void RogueConsole__flush__RogueString( RogueConsole* THISOBJ, RogueString* buffer_0 );
RogueLogical RogueConsole__has_another( RogueConsole* THISOBJ );
RogueConsoleMode* RogueConsole__mode( RogueConsole* THISOBJ );
RogueCharacter RogueConsole__read( RogueConsole* THISOBJ );
void RogueConsole__set_immediate_mode__RogueLogical( RogueConsole* THISOBJ, RogueLogical setting_0 );
RogueInt32 RogueConsole__width( RogueConsole* THISOBJ );
void RogueConsole__write__RogueString( RogueConsole* THISOBJ, RogueString* value_0 );
RogueLogical RogueConsole___fill_input_buffer__RogueInt32( RogueConsole* THISOBJ, RogueInt32 minimum_0 );
void RogueConsole__init_object( RogueConsole* THISOBJ );
RogueString* RogueConsole__toxRogueStringx( RogueConsole* THISOBJ );
RogueString* RogueConsole__type_name( RogueConsole* THISOBJ );
void RogueConsole__flush( RogueConsole* THISOBJ );
void RogueConsole__print__RogueCharacter( RogueConsole* THISOBJ, RogueCharacter value_0 );
void RogueConsole__print__RogueObject( RogueConsole* THISOBJ, RogueObject* value_0 );
void RogueConsole__print__RogueString( RogueConsole* THISOBJ, RogueString* value_0 );
void RogueConsole__println( RogueConsole* THISOBJ );
void RogueConsole__println__RogueObject( RogueConsole* THISOBJ, RogueObject* value_0 );
void RogueConsole__println__RogueString( RogueConsole* THISOBJ, RogueString* value_0 );
extern RogueObjectPoolxRogueStringx* RogueObjectPoolxRogueStringx_singleton;

RogueString* RogueObjectPoolxRogueStringx__on_use( RogueObjectPoolxRogueStringx* THISOBJ );
void RogueObjectPoolxRogueStringx__on_end_use__RogueString( RogueObjectPoolxRogueStringx* THISOBJ, RogueString* object_0 );
void RogueObjectPoolxRogueStringx__init_object( RogueObjectPoolxRogueStringx* THISOBJ );
RogueString* RogueObjectPoolxRogueStringx__type_name( RogueObjectPoolxRogueStringx* THISOBJ );
void RogueConsoleEventTypeList__init__RogueInt32( RogueConsoleEventTypeList* THISOBJ, RogueInt32 capacity_0 );
void RogueConsoleEventTypeList__init_object( RogueConsoleEventTypeList* THISOBJ );
void RogueConsoleEventTypeList__on_cleanup( RogueConsoleEventTypeList* THISOBJ );
void RogueConsoleEventTypeList__add__RogueConsoleEventType( RogueConsoleEventTypeList* THISOBJ, RogueConsoleEventType value_0 );
void RogueConsoleEventTypeList__clear( RogueConsoleEventTypeList* THISOBJ );
RogueString* RogueConsoleEventTypeList__description( RogueConsoleEventTypeList* THISOBJ );
void RogueConsoleEventTypeList__discard_from__RogueInt32( RogueConsoleEventTypeList* THISOBJ, RogueInt32 index_0 );
RogueConsoleEventType RogueConsoleEventTypeList__get__RogueInt32( RogueConsoleEventTypeList* THISOBJ, RogueInt32 index_0 );
void RogueConsoleEventTypeList__on_return_to_pool( RogueConsoleEventTypeList* THISOBJ );
void RogueConsoleEventTypeList__reserve__RogueInt32( RogueConsoleEventTypeList* THISOBJ, RogueInt32 additional_capacity_0 );
RogueString* RogueConsoleEventTypeList__toxRogueStringx( RogueConsoleEventTypeList* THISOBJ );
void RogueConsoleEventTypeList__zero__RogueInt32_RogueOptionalInt32( RogueConsoleEventTypeList* THISOBJ, RogueInt32 i1_0, RogueOptionalInt32 count_1 );
RogueString* RogueConsoleEventTypeList__type_name( RogueConsoleEventTypeList* THISOBJ );
void RogueConsoleErrorPrinter__flush__RogueString( RogueConsoleErrorPrinter* THISOBJ, RogueString* buffer_0 );
void RogueConsoleErrorPrinter__write__RogueString( RogueConsoleErrorPrinter* THISOBJ, RogueString* value_0 );
void RogueConsoleErrorPrinter__init_object( RogueConsoleErrorPrinter* THISOBJ );
RogueString* RogueConsoleErrorPrinter__type_name( RogueConsoleErrorPrinter* THISOBJ );
void RogueConsoleErrorPrinter__flush( RogueConsoleErrorPrinter* THISOBJ );
void RogueConsoleErrorPrinter__print__RogueCharacter( RogueConsoleErrorPrinter* THISOBJ, RogueCharacter value_0 );
void RogueConsoleErrorPrinter__print__RogueObject( RogueConsoleErrorPrinter* THISOBJ, RogueObject* value_0 );
void RogueConsoleErrorPrinter__print__RogueString( RogueConsoleErrorPrinter* THISOBJ, RogueString* value_0 );
void RogueConsoleErrorPrinter__println( RogueConsoleErrorPrinter* THISOBJ );
void RogueConsoleErrorPrinter__println__RogueObject( RogueConsoleErrorPrinter* THISOBJ, RogueObject* value_0 );
void RogueConsoleErrorPrinter__println__RogueString( RogueConsoleErrorPrinter* THISOBJ, RogueString* value_0 );
extern RogueFunction_260* RogueFunction_260_singleton;

void RogueFunction_260__call( RogueFunction_260* THISOBJ );
void RogueFunction_260__init_object( RogueFunction_260* THISOBJ );
RogueString* RogueFunction_260__type_name( RogueFunction_260* THISOBJ );
void RogueStandardConsoleMode__init( RogueStandardConsoleMode* THISOBJ );
RogueLogical RogueStandardConsoleMode__has_another( RogueStandardConsoleMode* THISOBJ );
RogueCharacter RogueStandardConsoleMode__read( RogueStandardConsoleMode* THISOBJ );
void RogueStandardConsoleMode__init_object( RogueStandardConsoleMode* THISOBJ );
RogueString* RogueStandardConsoleMode__type_name( RogueStandardConsoleMode* THISOBJ );
extern RogueFunction_262* RogueFunction_262_singleton;

void RogueFunction_262__call( RogueFunction_262* THISOBJ );
void RogueFunction_262__init_object( RogueFunction_262* THISOBJ );
RogueString* RogueFunction_262__type_name( RogueFunction_262* THISOBJ );
void RogueConsoleEventList__init( RogueConsoleEventList* THISOBJ );
void RogueConsoleEventList__init_object( RogueConsoleEventList* THISOBJ );
void RogueConsoleEventList__on_cleanup( RogueConsoleEventList* THISOBJ );
void RogueConsoleEventList__add__RogueConsoleEvent( RogueConsoleEventList* THISOBJ, RogueConsoleEvent value_0 );
void RogueConsoleEventList__clear( RogueConsoleEventList* THISOBJ );
void RogueConsoleEventList__copy__RogueInt32_RogueInt32_RogueConsoleEventList_RogueInt32( RogueConsoleEventList* THISOBJ, RogueInt32 src_i1_0, RogueInt32 src_count_1, RogueConsoleEventList* dest_2, RogueInt32 dest_i1_3 );
RogueString* RogueConsoleEventList__description( RogueConsoleEventList* THISOBJ );
void RogueConsoleEventList__discard_from__RogueInt32( RogueConsoleEventList* THISOBJ, RogueInt32 index_0 );
void RogueConsoleEventList__ensure_capacity__RogueInt32( RogueConsoleEventList* THISOBJ, RogueInt32 desired_capacity_0 );
void RogueConsoleEventList__expand_to_count__RogueInt32( RogueConsoleEventList* THISOBJ, RogueInt32 minimum_count_0 );
RogueConsoleEvent RogueConsoleEventList__get__RogueInt32( RogueConsoleEventList* THISOBJ, RogueInt32 index_0 );
void RogueConsoleEventList__on_return_to_pool( RogueConsoleEventList* THISOBJ );
RogueConsoleEvent RogueConsoleEventList__remove_first( RogueConsoleEventList* THISOBJ );
void RogueConsoleEventList__reserve__RogueInt32( RogueConsoleEventList* THISOBJ, RogueInt32 additional_capacity_0 );
void RogueConsoleEventList__shift__RogueInt32( RogueConsoleEventList* THISOBJ, RogueInt32 delta_0 );
RogueString* RogueConsoleEventList__toxRogueStringx( RogueConsoleEventList* THISOBJ );
void RogueConsoleEventList__zero__RogueInt32_RogueOptionalInt32( RogueConsoleEventList* THISOBJ, RogueInt32 i1_0, RogueOptionalInt32 count_1 );
RogueString* RogueConsoleEventList__type_name( RogueConsoleEventList* THISOBJ );
void RogueImmediateConsoleMode__init( RogueImmediateConsoleMode* THISOBJ );
void RogueImmediateConsoleMode___on_exit( RogueImmediateConsoleMode* THISOBJ );
RogueLogical RogueImmediateConsoleMode__has_another( RogueImmediateConsoleMode* THISOBJ );
RogueCharacter RogueImmediateConsoleMode__read( RogueImmediateConsoleMode* THISOBJ );
void RogueImmediateConsoleMode___fill_event_queue( RogueImmediateConsoleMode* THISOBJ );
void RogueImmediateConsoleMode___fill_event_queue_windows( RogueImmediateConsoleMode* THISOBJ );
void RogueImmediateConsoleMode___fill_event_queue_windows_process_next( RogueImmediateConsoleMode* THISOBJ );
void RogueImmediateConsoleMode___fill_event_queue_windows_process_next__RogueWindowsInputRecord( RogueImmediateConsoleMode* THISOBJ, RogueWindowsInputRecord record_0 );
void RogueImmediateConsoleMode___fill_event_queue_unix( RogueImmediateConsoleMode* THISOBJ );
void RogueImmediateConsoleMode___fill_event_queue_unix_process_next( RogueImmediateConsoleMode* THISOBJ );
void RogueImmediateConsoleMode__init_object( RogueImmediateConsoleMode* THISOBJ );
RogueString* RogueImmediateConsoleMode__type_name( RogueImmediateConsoleMode* THISOBJ );
void RogueUnixConsoleMouseEventTypeList__init__RogueInt32( RogueUnixConsoleMouseEventTypeList* THISOBJ, RogueInt32 capacity_0 );
void RogueUnixConsoleMouseEventTypeList__init_object( RogueUnixConsoleMouseEventTypeList* THISOBJ );
void RogueUnixConsoleMouseEventTypeList__on_cleanup( RogueUnixConsoleMouseEventTypeList* THISOBJ );
void RogueUnixConsoleMouseEventTypeList__add__RogueUnixConsoleMouseEventType( RogueUnixConsoleMouseEventTypeList* THISOBJ, RogueUnixConsoleMouseEventType value_0 );
void RogueUnixConsoleMouseEventTypeList__clear( RogueUnixConsoleMouseEventTypeList* THISOBJ );
RogueString* RogueUnixConsoleMouseEventTypeList__description( RogueUnixConsoleMouseEventTypeList* THISOBJ );
void RogueUnixConsoleMouseEventTypeList__discard_from__RogueInt32( RogueUnixConsoleMouseEventTypeList* THISOBJ, RogueInt32 index_0 );
RogueUnixConsoleMouseEventType RogueUnixConsoleMouseEventTypeList__get__RogueInt32( RogueUnixConsoleMouseEventTypeList* THISOBJ, RogueInt32 index_0 );
void RogueUnixConsoleMouseEventTypeList__on_return_to_pool( RogueUnixConsoleMouseEventTypeList* THISOBJ );
void RogueUnixConsoleMouseEventTypeList__reserve__RogueInt32( RogueUnixConsoleMouseEventTypeList* THISOBJ, RogueInt32 additional_capacity_0 );
RogueString* RogueUnixConsoleMouseEventTypeList__toxRogueStringx( RogueUnixConsoleMouseEventTypeList* THISOBJ );
void RogueUnixConsoleMouseEventTypeList__zero__RogueInt32_RogueOptionalInt32( RogueUnixConsoleMouseEventTypeList* THISOBJ, RogueInt32 i1_0, RogueOptionalInt32 count_1 );
RogueString* RogueUnixConsoleMouseEventTypeList__type_name( RogueUnixConsoleMouseEventTypeList* THISOBJ );
void* Rogue_dispatch_type_name___RogueObject( void* THISOBJ );
void Rogue_dispatch_flush_( void* THISOBJ );
void Rogue_dispatch_print__RogueCharacter( void* THISOBJ, RogueCharacter p0 );
void Rogue_dispatch_print__RogueString( void* THISOBJ, RogueString* p0 );
void Rogue_dispatch_println__RogueObject( void* THISOBJ, RogueObject* p0 );
void Rogue_dispatch_println__RogueString( void* THISOBJ, RogueString* p0 );
void Rogue_dispatch_on_return_to_pool_( void* THISOBJ );

extern RogueInt32 Rogue_type_count;
extern RogueRuntimeType* Rogue_types[64];
extern RogueInt32 Rogue_base_types[62];
extern RogueString* Rogue_string_table[44];
extern RogueString* str____SEGFAULT___;
extern RogueString* str_null;
extern RogueString* str_;
extern RogueString* str__9223372036854775808;
extern RogueString* str_Hello_Rogue_;
extern RogueString* str_StackTrace_init__;
extern RogueString* str_INTERNAL;
extern RogueString* str_Unknown_Procedure;
extern RogueString* str__Call_history_unavai;
extern RogueString* str__;
extern RogueString* str___;
extern RogueString* str___1;
extern RogueString* str___2;
extern RogueString* str____;
extern RogueString* str____25;
extern RogueString* str_BACKSPACE;
extern RogueString* str_TAB;
extern RogueString* str_NEWLINE;
extern RogueString* str_ESCAPE;
extern RogueString* str_UP;
extern RogueString* str_DOWN;
extern RogueString* str_RIGHT;
extern RogueString* str_LEFT;
extern RogueString* str_DELETE;
extern RogueString* str___3;
extern RogueString* str___4;
extern RogueString* str___5;
extern RogueString* str_CHARACTER;
extern RogueString* str_POINTER_PRESS_LEFT;
extern RogueString* str_POINTER_PRESS_RIGHT;
extern RogueString* str_POINTER_RELEASE;
extern RogueString* str_POINTER_MOVE;
extern RogueString* str_SCROLL_UP;
extern RogueString* str_SCROLL_DOWN;
extern RogueString* str_ConsoleEventType_;
extern RogueString* str____1003h;
extern RogueString* str____1003l;
extern RogueString* str_PRESS_LEFT;
extern RogueString* str_PRESS_RIGHT;
extern RogueString* str_RELEASE;
extern RogueString* str_DRAG_LEFT;
extern RogueString* str_DRAG_RIGHT;
extern RogueString* str_MOVE;
extern RogueString* str_UnixConsoleMouseEven;
END_ROGUE_EXTERN_C
#endif // TEST_H
