// Test.c

#include "Test.h"
#include <stdio.h>

//------------------------------------------------------------------------------
// Runtime
//------------------------------------------------------------------------------
void  Rogue_configure( int argc, char** argv )
{
  Rogue_argc = argc;
  Rogue_argv = argv;
}

void* Rogue_create_object( void* _type )
{
  #if defined(ROGUE_GC_AUTO)
    if (RogueMM_bytes_allocated_since_gc >= ROGUE_MM_GC_THRESHOLD) Rogue_collect_garbage();
  #endif
  RogueRuntimeType*  type = (RogueRuntimeType*) _type;
  RogueObject* obj = (RogueObject*) ROGUE_MALLOC( type->size );
  memset( obj, 0, type->size );
  obj->__type = type;

  if (type->fn_on_cleanup) RogueObjectList_add( &RogueMM_objects_requiring_cleanup, obj );
  else                     RogueObjectList_add( &RogueMM_objects, obj );
  RogueMM_bytes_allocated_since_gc += type->size;

  if (type->fn_init_object)
  {
    #ifdef ROGUE_GC_AUTO
      obj->__refcount = 1; // keeps this object alive if init() triggers a GC
      type->fn_init_object( obj );
      --obj->__refcount;   // other code maybe retained this obj so don't just set back to 0.
    #else
      type->fn_init_object( obj );
    #endif
  }

  return obj;
}

void Rogue_destroy_object( void* obj )
{
  //printf( "Destroy object %s\n", ((RogueObject*)obj)->__type->name );
  ROGUE_FREE( obj );
}

void* Rogue_new_object( void* _type )
{
  // Create a Rogue object that is not tracked by the GC.
  RogueRuntimeType*  type = (RogueRuntimeType*) _type;
  RogueObject* obj = (RogueObject*) ROGUE_MALLOC( type->size );
  memset( obj, 0, type->size );
  obj->__type = type;

  if (type->fn_init_object)
  {
    type->fn_init_object( obj );
  }

  return obj;
}

void* Rogue_singleton( void* _type, void* singleton_ref )
{
  if (*((RogueObject**)singleton_ref)) return *((RogueObject**)singleton_ref);
  RogueRuntimeType* type = (RogueRuntimeType*) _type;
  RogueObject* obj = (RogueObject*)Rogue_create_object( type );
  (*((RogueObject**)singleton_ref)) = obj;
  if (type->fn_init) type->fn_init(obj);  // call init() if it exists
  if (type->fn_on_singleton_change) type->fn_on_singleton_change( 0, obj );
  return obj;
}

void Rogue_set_singleton( void* _type, void* singleton_ref, void* new_singleton )
{
  RogueObject** ref = (RogueObject**)singleton_ref;
  RogueRuntimeType* type = (RogueRuntimeType*) _type;
  if (type->fn_on_singleton_change) type->fn_on_singleton_change( *ref, new_singleton );
  *ref = (RogueObject*)new_singleton;
}

void* Rogue_release( void* obj )
{
  if (obj) --(((RogueObject*)obj)->__refcount);
  return 0;
}

void* Rogue_retain( void* obj )
{
  if (obj) ++(((RogueObject*)obj)->__refcount);
  return obj;
}

void Rogue_check_gc(void)
{
  if (RogueMM_bytes_allocated_since_gc >= ROGUE_MM_GC_THRESHOLD || RogueMM_gc_request) Rogue_collect_garbage();
}

void Rogue_request_gc(void)
{
  Rogue_collect_garbage();
}

int Rogue_quit(void)
{
  int status = !!Rogue_exception;
  Rogue_print_exception();  // Only has an effect if there is a pending exception

  RogueGlobal__call_exit_functions();

  return status;
}

void Rogue_exit( int exit_code )
{
  // Just calls exit(), but if we call exit() directly from Rogue code then the C compiler
  // may give an unreachable code warning for the call stack cleanup that follows.
  exit( exit_code );
}

void Rogue_print_exception(void)
{
  RogueException__display__RogueException( (RogueException*)Rogue_exception );
}

void Rogue_call_stack_push( const char* procedure, const char* filename, int line )
{
  if (Rogue_call_stack_count) Rogue_call_stack[ Rogue_call_stack_count-1 ].line = Rogue_call_stack_line;
  Rogue_call_stack_line = line;

  if (Rogue_call_stack_count == Rogue_call_stack_capacity)
  {
    int required_capacity = Rogue_call_stack_capacity << 1;
    if (required_capacity < 32) required_capacity = 32;

    RogueCallFrame* new_data = (RogueCallFrame*) ROGUE_MALLOC( sizeof(RogueCallFrame) * required_capacity );
    if (Rogue_call_stack)
    {
      memcpy( new_data, Rogue_call_stack, sizeof(RogueCallFrame)*Rogue_call_stack_count );
      ROGUE_FREE( Rogue_call_stack );
    }
    Rogue_call_stack = new_data;
    Rogue_call_stack_capacity = required_capacity;
  }

  Rogue_call_stack[ Rogue_call_stack_count++ ] = ROGUE_COMPOUND(RogueCallFrame){ procedure, filename, line };
}

void Rogue_call_stack_pop(void)
{
  if (--Rogue_call_stack_count)
  {
    Rogue_call_stack_line = Rogue_call_stack[ Rogue_call_stack_count-1 ].line;
  }
}

void RogueRuntimeType_local_pointer_stack_add( RogueRuntimeType* type, void* local_pointer )
{
  if (type->local_pointer_count == type->local_pointer_capacity)
  {
    int required_capacity = type->local_pointer_capacity << 1;
    if (required_capacity < 32) required_capacity = 32;

    void** new_data = (void**) ROGUE_MALLOC( sizeof(void*) * required_capacity );
    if (type->local_pointer_stack)
    {
      memcpy( new_data, type->local_pointer_stack, sizeof(void*)*type->local_pointer_count );
      ROGUE_FREE( type->local_pointer_stack );
    }
    type->local_pointer_stack = new_data;
    type->local_pointer_capacity = required_capacity;
  }
  type->local_pointer_stack[ type->local_pointer_count++ ] = local_pointer;
}

void RogueObjectList_add( RogueObjectList* list, void* obj )
{
  if (list->count == list->capacity)
  {
    int required_capacity = list->capacity << 1;
    if (required_capacity < 1024) required_capacity = 1024;

    RogueObject** new_data = (RogueObject**) ROGUE_MALLOC( sizeof(RogueObject*) * required_capacity );
    if (list->data)
    {
      memcpy( new_data, list->data, sizeof(RogueObject*)*list->count );
      ROGUE_FREE( list->data );
    }
    list->data = new_data;
    list->capacity = required_capacity;
  }

  list->data[ list->count++ ] = (RogueObject*)obj;
}

RogueObject*     Rogue_exception  = 0;
RogueCallFrame*  Rogue_call_stack = 0;
int              Rogue_call_stack_count    = 0;
int              Rogue_call_stack_capacity = 0;
int              Rogue_call_stack_line     = 0;

int              RogueMM_bytes_allocated_since_gc  = 0;
int              RogueMM_gc_request                = 0;
RogueObjectList  RogueMM_objects                   = {0};
RogueObjectList  RogueMM_objects_requiring_cleanup = {0};

void* Rogue_as( void* obj, RogueInt32 recast_type_id )
{
  if ( !obj ) return 0;
  RogueRuntimeType* type = ((RogueObject*)obj)->__type;
  if (type->id == recast_type_id) return obj;

  RogueInt32* base_ids = type->base_type_ids - 1;
  RogueInt32  count = type->base_type_count;
  while (--count >= 0)
  {
    if (*(++base_ids) == recast_type_id) return obj;
  }
  return 0;
}

RogueLogical Rogue_instance_of( void* obj, RogueInt32 ancestor_id )
{
  if ( !obj ) return 0;
  RogueRuntimeType* type = ((RogueObject*)obj)->__type;
  if (type->id == ancestor_id) return 1;

  RogueInt32* base_ids = type->base_type_ids - 1;
  RogueInt32  count = type->base_type_count;
  while (--count >= 0)
  {
    if (*(++base_ids) == ancestor_id) return 1;
  }
  return 0;
}

RogueLogical Rogue_is_type( void* obj, RogueRuntimeType* type )
{
  if ( !obj ) return 0;
  return ((RogueObject*)obj)->__type == type;
}


RogueInt32 Rogue_type_count = 64;
RogueRuntimeType TypeRogueLogical = { "Logical", 0, 0, 1, 0, 0, 1073742145};
RogueRuntimeType TypeRogueByte = { "Byte", 0, 1, 2, 0, 0, 1073742145};
RogueRuntimeType TypeRogueCharacter = { "Character", 0, 2, 3, 0, 0, 1073742145};

void* TypeRogueInt32_vtable[4] = {(void*)RogueInt32__abs,(void*)RogueInt32__clamped_low__RogueInt32,(void*)RogueInt32__digit_count,(void*)RogueInt32__or_larger__RogueInt32};

RogueRuntimeType TypeRogueInt32 = { "Int32", 0, 3, 4, 0, 0, 1073742145};

void* TypeRogueInt64_vtable[1] = {(void*)RogueInt64__operatorMOD__RogueInt64};

RogueRuntimeType TypeRogueInt64 = { "Int64", 0, 4, 5, 0, 0, 1073742145};
RogueRuntimeType TypeRogueReal32 = { "Real32", 0, 5, 6, 0, 0, 1073742145};
RogueRuntimeType TypeRogueReal64 = { "Real64", 0, 6, 7, 0, 0, 1073742145};
RogueRuntimeType TypeRogueRogueCNativeProperty = { "RogueCNativeProperty", 0, 7, 13, 0, 0, 1073742081};

void* TypeRogueStackTraceFrame_vtable[2] = {(void*)RogueStackTraceFrame__toxRogueStringx,(void*)RogueStackTraceFrame__toxRogueStringx__RogueInt32_RogueInt32};

RogueRuntimeType TypeRogueStackTraceFrame = { "StackTraceFrame", 0, 8, 40, 0, 0, 1073807618, TypeRogueStackTraceFrame_vtable, 0, 0, 0, sizeof(RogueStackTraceFrame), Rogue_base_types+26, 0, 0, 0, 0, 0, 0, 0 };
RogueRuntimeType TypeRogueOptionalInt32 = { "Int32?", 0, 9, 25, 0, 0, 1077936386, 0, 0, 0, 0, sizeof(RogueOptionalInt32), Rogue_base_types+22, 1, 0, 0, 0, 0, 0, 0 };

void* TypeRogueConsoleCursor_vtable[2] = {(void*)RogueConsoleCursor__hide__RogueLogical_RoguePrintWriter,(void*)RogueConsoleCursor__show__RoguePrintWriter};

RogueRuntimeType TypeRogueConsoleCursor = { "ConsoleCursor", 0, 10, 49, 0, 0, 1073742082, TypeRogueConsoleCursor_vtable, 0, 0, 0, sizeof(RogueConsoleCursor), Rogue_base_types+41, 0, 0, 0, 0, 0, 0, 0 };
RogueLogical RogueConsoleCursor__g_cursor_hidden = 0;

void* TypeRogueConsoleEventType_vtable[2] = {(void*)RogueConsoleEventType__operator____RogueConsoleEventType,(void*)RogueConsoleEventType__toxRogueStringx};

RogueRuntimeType TypeRogueConsoleEventType = { "ConsoleEventType", 0, 11, 52, 0, 0, 1073742086, TypeRogueConsoleEventType_vtable, 0, 0, 0, sizeof(RogueConsoleEventType), Rogue_base_types+42, 0, 0, 0, 0, 0, 0, 0 };
RogueConsoleEventTypeList* RogueConsoleEventType__g_categories = 0;

void* TypeRogueConsoleEvent_vtable[2] = {(void*)RogueConsoleEvent__is_character,(void*)RogueConsoleEvent__toxRogueStringx};

RogueRuntimeType TypeRogueConsoleEvent = { "ConsoleEvent", 0, 12, 51, 0, 0, 1073742082, TypeRogueConsoleEvent_vtable, 0, 0, 0, sizeof(RogueConsoleEvent), Rogue_base_types+42, 0, 0, 0, 0, 0, 0, 0 };

void* TypeRogueRangeUpToLessThanxRogueInt32x_vtable[1] = {(void*)RogueRangeUpToLessThanxRogueInt32x__iterator};

RogueRuntimeType TypeRogueRangeUpToLessThanxRogueInt32x = { "RangeUpToLessThan<<Rogue::Int32>>", 0, 13, 55, 0, 0, 1073742082, TypeRogueRangeUpToLessThanxRogueInt32x_vtable, 0, 0, 0, sizeof(RogueRangeUpToLessThanxRogueInt32x), Rogue_base_types+48, 0, 0, 0, 0, 0, 0, 0 };

void* TypeRogueRangeUpToLessThanIteratorxRogueInt32x_vtable[1] = {(void*)RogueRangeUpToLessThanIteratorxRogueInt32x__read_another};

RogueRuntimeType TypeRogueRangeUpToLessThanIteratorxRogueInt32x = { "RangeUpToLessThanIterator<<Rogue::Int32>>", 0, 14, 56, 0, 0, 1073742082, TypeRogueRangeUpToLessThanIteratorxRogueInt32x_vtable, 0, 0, 0, sizeof(RogueRangeUpToLessThanIteratorxRogueInt32x), Rogue_base_types+48, 0, 0, 0, 0, 0, 0, 0 };
RogueRuntimeType TypeRogueWindowsInputRecord = { "WindowsInputRecord", 0, 15, 62, 0, 0, 1073742082, 0, 0, 0, 0, sizeof(RogueWindowsInputRecord), Rogue_base_types+59, 0, 0, 0, 0, 0, 0, 0 };

void* TypeRogueUnixConsoleMouseEventType_vtable[1] = {(void*)RogueUnixConsoleMouseEventType__toxRogueStringx};

RogueRuntimeType TypeRogueUnixConsoleMouseEventType = { "UnixConsoleMouseEventType", 0, 16, 63, 0, 0, 1073742086, TypeRogueUnixConsoleMouseEventType_vtable, 0, 0, 0, sizeof(RogueUnixConsoleMouseEventType), Rogue_base_types+59, 0, 0, 0, 0, 0, 0, 0 };
RogueUnixConsoleMouseEventTypeList* RogueUnixConsoleMouseEventType__g_categories = 0;

void* TypeRogueByteList_vtable[21] = {(void*)RogueByteList__toxRogueStringx,(void*)RogueByteList__init_object,(void*)RogueByteList__type_name,(void*)RogueByteList__init,(void*)RogueByteList__init__RogueInt32,(void*)RogueByteList__on_cleanup,(void*)RogueByteList__add__RogueByte,(void*)RogueByteList__clear,(void*)RogueByteList__copy__RogueInt32_RogueInt32_RogueByteList_RogueInt32,(void*)RogueByteList__description,(void*)RogueByteList__discard_from__RogueInt32,(void*)RogueByteList__ensure_capacity__RogueInt32,(void*)RogueByteList__expand_to_count__RogueInt32,(void*)RogueByteList__first,(void*)RogueByteList__get__RogueInt32,(void*)RogueByteList__on_return_to_pool,(void*)RogueByteList__remove_first,(void*)RogueByteList__reserve__RogueInt32,(void*)RogueByteList__set__RogueInt32_RogueByte,(void*)RogueByteList__shift__RogueInt32,(void*)RogueByteList__zero__RogueInt32_RogueOptionalInt32};

RogueRuntimeType TypeRogueByteList = { "Byte[]", 0, 17, 22, 0, 0, 1073742088, TypeRogueByteList_vtable, 0, 0, 0, sizeof(RogueByteList), Rogue_base_types+13, 3, 0, (RogueFn_Object)RogueByteList__init_object, (RogueFn_Object)RogueByteList__init, (RogueFn_Object)RogueObject_gc_trace, (RogueFn_Object)RogueByteList__on_cleanup, 0 };

void* TypeRogueString_vtable[24] = {(void*)RogueString__toxRogueStringx,(void*)RogueString__init_object,(void*)RogueString__type_name,(void*)RogueString__init,(void*)RogueString__init__RogueInt32,(void*)RogueString__clear,(void*)RogueString__count,(void*)RogueString__flush,(void*)RogueString__from__RogueInt32_RogueInt32,(void*)RogueString__get__RogueInt32,(void*)RogueString__hash_code,(void*)RogueString__justified__RogueInt32_RogueCharacter,(void*)RogueString__leftmost__RogueInt32,(void*)RogueString__on_return_to_pool,(void*)RogueString__print__RogueByte,(void*)RogueString__print__RogueCharacter,(void*)RogueString__print__RogueInt32,(void*)RogueString__print__RogueInt64,(void*)RogueString__print__RogueObject,(void*)RogueString__print__RogueString,(void*)RogueString__println__RogueObject,(void*)RogueString__println__RogueString,(void*)RogueString__reserve__RogueInt32,(void*)RogueString__set_cursor__RogueInt32};

RogueRuntimeType TypeRogueString = { "String", 0, 18, 11, 0, 0, 1073807688, TypeRogueString_vtable, 0, 0, 0, sizeof(RogueString), Rogue_base_types+8, 3, 0, (RogueFn_Object)RogueString__init_object, (RogueFn_Object)RogueString__init, (RogueFn_Object)RogueString_gc_trace, 0, 0 };
RogueRuntimeType TypeRoguePrintWriter = { "PrintWriter", 0, 19, 21, 0, 0, 1090519312};

void* TypeRogueOPARENFunctionOPARENCPARENCPAREN_vtable[4] = {(void*)RogueObject__toxRogueStringx,(void*)RogueOPARENFunctionOPARENCPARENCPAREN__init_object,(void*)RogueOPARENFunctionOPARENCPARENCPAREN__type_name,0};

RogueRuntimeType TypeRogueOPARENFunctionOPARENCPARENCPAREN = { "(Function())", 0, 20, 43, 0, 0, 1090519304, TypeRogueOPARENFunctionOPARENCPARENCPAREN_vtable, 0, 0, 0, sizeof(RogueOPARENFunctionOPARENCPARENCPAREN), Rogue_base_types+30, 1, 0, (RogueFn_Object)RogueOPARENFunctionOPARENCPARENCPAREN__init_object, 0, (RogueFn_Object)RogueObject_gc_trace, 0, 0 };

void* TypeRogueOPARENFunctionOPARENCPARENCPARENList_vtable[13] = {(void*)RogueOPARENFunctionOPARENCPARENCPARENList__toxRogueStringx,(void*)RogueOPARENFunctionOPARENCPARENCPARENList__init_object,(void*)RogueOPARENFunctionOPARENCPARENCPARENList__type_name,(void*)RogueOPARENFunctionOPARENCPARENCPARENList__init,(void*)RogueOPARENFunctionOPARENCPARENCPARENList__on_cleanup,(void*)RogueOPARENFunctionOPARENCPARENCPARENList__add__RogueOPARENFunctionOPARENCPARENCPAREN,(void*)RogueOPARENFunctionOPARENCPARENCPARENList__clear,(void*)RogueOPARENFunctionOPARENCPARENCPARENList__description,(void*)RogueOPARENFunctionOPARENCPARENCPARENList__discard_from__RogueInt32,(void*)RogueOPARENFunctionOPARENCPARENCPARENList__get__RogueInt32,(void*)RogueOPARENFunctionOPARENCPARENCPARENList__on_return_to_pool,(void*)RogueOPARENFunctionOPARENCPARENCPARENList__reserve__RogueInt32,(void*)RogueOPARENFunctionOPARENCPARENCPARENList__zero__RogueInt32_RogueOptionalInt32};

RogueRuntimeType TypeRogueOPARENFunctionOPARENCPARENCPARENList = { "(Function())[]", 0, 21, 44, 0, 0, 1074856200, TypeRogueOPARENFunctionOPARENCPARENCPARENList_vtable, 0, 0, 0, sizeof(RogueOPARENFunctionOPARENCPARENCPARENList), Rogue_base_types+31, 3, 0, (RogueFn_Object)RogueOPARENFunctionOPARENCPARENCPARENList__init_object, (RogueFn_Object)RogueOPARENFunctionOPARENCPARENCPARENList__init, (RogueFn_Object)RogueOPARENFunctionOPARENCPARENCPARENList_gc_trace, (RogueFn_Object)RogueOPARENFunctionOPARENCPARENCPARENList__on_cleanup, 0 };

void* TypeRogueGlobal_vtable[14] = {(void*)RogueObject__toxRogueStringx,(void*)RogueGlobal__init_object,(void*)RogueGlobal__type_name,(void*)RogueGlobal__init,(void*)RogueGlobal__configure_standard_output,(void*)RogueGlobal__flush__RogueString,(void*)RogueGlobal__on_exit__RogueOPARENFunctionOPARENCPARENCPAREN,(void*)RogueGlobal__flush,(void*)RogueGlobal__print__RogueCharacter,(void*)RogueGlobal__print__RogueObject,(void*)RogueGlobal__print__RogueString,(void*)RogueGlobal__println,(void*)RogueGlobal__println__RogueObject,(void*)RogueGlobal__println__RogueString};

RogueRuntimeType TypeRogueGlobal = { "Global", 0, 22, 8, 0, 0, 1075904904, TypeRogueGlobal_vtable, 0, 0, 0, sizeof(RogueGlobal), Rogue_base_types+5, 3, 0, (RogueFn_Object)RogueGlobal__init_object, (RogueFn_Object)RogueGlobal__init, (RogueFn_Object)RogueGlobal_gc_trace, 0, 0 };
RogueGlobal* RogueGlobal_singleton = 0;
RogueRuntimeType TypeRogueListType = { "ListType", 0, 23, 9, 0, 0, 1073742096};

void* TypeRogueObject_vtable[3] = {(void*)RogueObject__toxRogueStringx,(void*)RogueObject__init_object,(void*)RogueObject__type_name};

RogueRuntimeType TypeRogueObject = { "Object", 0, 24, 10, 0, 0, 1073742088, TypeRogueObject_vtable, 0, 0, 0, sizeof(RogueObject), Rogue_base_types+8, 0, 0, (RogueFn_Object)RogueObject__init_object, 0, (RogueFn_Object)RogueObject_gc_trace, 0, 0 };

void* TypeRogueStackTraceFrameList_vtable[15] = {(void*)RogueStackTraceFrameList__toxRogueStringx,(void*)RogueStackTraceFrameList__init_object,(void*)RogueStackTraceFrameList__type_name,(void*)RogueStackTraceFrameList__init__RogueInt32,(void*)RogueStackTraceFrameList__on_cleanup,(void*)RogueStackTraceFrameList__add__RogueStackTraceFrame,(void*)RogueStackTraceFrameList__clear,(void*)RogueStackTraceFrameList__description,(void*)RogueStackTraceFrameList__discard_from__RogueInt32,(void*)RogueStackTraceFrameList__get__RogueInt32,(void*)RogueStackTraceFrameList__last,(void*)RogueStackTraceFrameList__on_return_to_pool,(void*)RogueStackTraceFrameList__remove_last,(void*)RogueStackTraceFrameList__reserve__RogueInt32,(void*)RogueStackTraceFrameList__zero__RogueInt32_RogueOptionalInt32};

RogueRuntimeType TypeRogueStackTraceFrameList = { "StackTraceFrame[]", 0, 25, 41, 0, 0, 1073807624, TypeRogueStackTraceFrameList_vtable, 0, 0, 0, sizeof(RogueStackTraceFrameList), Rogue_base_types+26, 3, 0, (RogueFn_Object)RogueStackTraceFrameList__init_object, 0, (RogueFn_Object)RogueStackTraceFrameList_gc_trace, (RogueFn_Object)RogueStackTraceFrameList__on_cleanup, 0 };

void* TypeRogueStackTrace_vtable[7] = {(void*)RogueStackTrace__toxRogueStringx,(void*)RogueStackTrace__init_object,(void*)RogueStackTrace__type_name,(void*)RogueStackTrace__init,(void*)RogueStackTrace__filename__RogueInt32,(void*)RogueStackTrace__line__RogueInt32,(void*)RogueStackTrace__procedure__RogueInt32};

RogueRuntimeType TypeRogueStackTrace = { "StackTrace", 0, 26, 39, 0, 0, 1073807624, TypeRogueStackTrace_vtable, 0, 0, 0, sizeof(RogueStackTrace), Rogue_base_types+25, 1, 0, (RogueFn_Object)RogueStackTrace__init_object, (RogueFn_Object)RogueStackTrace__init, (RogueFn_Object)RogueStackTrace_gc_trace, 0, 0 };

void* TypeRogueException_vtable[4] = {(void*)RogueException__toxRogueStringx,(void*)RogueException__init_object,(void*)RogueException__type_name,(void*)RogueException__display};

RogueRuntimeType TypeRogueException = { "Exception", 0, 27, 12, 0, 0, 1073807688, TypeRogueException_vtable, 0, 0, 0, sizeof(RogueException), Rogue_base_types+11, 1, 0, (RogueFn_Object)RogueException__init_object, 0, (RogueFn_Object)RogueException_gc_trace, 0, 0 };

void* TypeRogueRoutine_vtable[3] = {(void*)RogueObject__toxRogueStringx,(void*)RogueRoutine__init_object,(void*)RogueRoutine__type_name};

RogueRuntimeType TypeRogueRoutine = { "Routine", 0, 28, 14, 0, 0, 1073742152, TypeRogueRoutine_vtable, 0, 0, 0, sizeof(RogueRoutine), Rogue_base_types+12, 1, 0, (RogueFn_Object)RogueRoutine__init_object, 0, (RogueFn_Object)RogueObject_gc_trace, 0, 0 };
RogueRuntimeType TypeRogueAugment_0_Routine_Rogue = { "Augment_0_Routine_Rogue", 0, 29, 15, 0, 0, 1140850960};
RogueRuntimeType TypeRogueAugment_1_Routine_Rogue = { "Augment_1_Routine_Rogue", 0, 30, 16, 0, 0, 1140850960};
RogueRuntimeType TypeRogueAugment_2_Routine_Rogue = { "Augment_2_Routine_Rogue", 0, 31, 17, 0, 0, 1140850960};
RogueRuntimeType TypeRogueAugment_22_Routine_Rogue = { "Augment_22_Routine_Rogue", 0, 32, 18, 0, 0, 1140850960};
RogueRuntimeType TypeRogueAugment_23_Routine_Rogue = { "Augment_23_Routine_Rogue", 0, 33, 19, 0, 0, 1140850960};
RogueRuntimeType TypeRoguePoolable = { "Poolable", 0, 34, 20, 0, 0, 1073742096};

void* TypeRogueCharacterList_vtable[13] = {(void*)RogueCharacterList__toxRogueStringx,(void*)RogueCharacterList__init_object,(void*)RogueCharacterList__type_name,(void*)RogueCharacterList__init,(void*)RogueCharacterList__on_cleanup,(void*)RogueCharacterList__add__RogueCharacter,(void*)RogueCharacterList__clear,(void*)RogueCharacterList__description,(void*)RogueCharacterList__discard_from__RogueInt32,(void*)RogueCharacterList__get__RogueInt32,(void*)RogueCharacterList__on_return_to_pool,(void*)RogueCharacterList__reserve__RogueInt32,(void*)RogueCharacterList__zero__RogueInt32_RogueOptionalInt32};

RogueRuntimeType TypeRogueCharacterList = { "Character[]", 0, 35, 23, 0, 0, 1073742088, TypeRogueCharacterList_vtable, 0, 0, 0, sizeof(RogueCharacterList), Rogue_base_types+16, 3, 0, (RogueFn_Object)RogueCharacterList__init_object, (RogueFn_Object)RogueCharacterList__init, (RogueFn_Object)RogueObject_gc_trace, (RogueFn_Object)RogueCharacterList__on_cleanup, 0 };

void* TypeRogueStringList_vtable[15] = {(void*)RogueStringList__toxRogueStringx,(void*)RogueStringList__init_object,(void*)RogueStringList__type_name,(void*)RogueStringList__init,(void*)RogueStringList__on_cleanup,(void*)RogueStringList__add__RogueString,(void*)RogueStringList__clear,(void*)RogueStringList__description,(void*)RogueStringList__discard_from__RogueInt32,(void*)RogueStringList__get__RogueInt32,(void*)RogueStringList__is_empty,(void*)RogueStringList__on_return_to_pool,(void*)RogueStringList__remove_last,(void*)RogueStringList__reserve__RogueInt32,(void*)RogueStringList__zero__RogueInt32_RogueOptionalInt32};

RogueRuntimeType TypeRogueStringList = { "String[]", 0, 36, 24, 0, 0, 1074856200, TypeRogueStringList_vtable, 0, 0, 0, sizeof(RogueStringList), Rogue_base_types+19, 3, 0, (RogueFn_Object)RogueStringList__init_object, (RogueFn_Object)RogueStringList__init, (RogueFn_Object)RogueStringList_gc_trace, (RogueFn_Object)RogueStringList__on_cleanup, 0 };
RogueRuntimeType TypeRogueAugment_26_Rogue_String = { "Augment_26_Rogue_String", 0, 37, 26, 0, 0, 1140850960};

void* TypeRogueStringPool_vtable[5] = {(void*)RogueObject__toxRogueStringx,(void*)RogueStringPool__init_object,(void*)RogueStringPool__type_name,(void*)RogueObjectPoolxRogueStringx__on_use,(void*)RogueObjectPoolxRogueStringx__on_end_use__RogueString};

RogueRuntimeType TypeRogueStringPool = { "StringPool", 0, 38, 27, 0, 0, 1075904904, TypeRogueStringPool_vtable, 0, 0, 0, sizeof(RogueStringPool), Rogue_base_types+23, 2, 0, (RogueFn_Object)RogueStringPool__init_object, 0, (RogueFn_Object)RogueStringPool_gc_trace, 0, 0 };
RogueStringPool* RogueStringPool_singleton = 0;
RogueRuntimeType TypeRogueCommonPrimitiveMethodsxRogueInt32x = { "CommonPrimitiveMethods<<Rogue::Int32>>", 0, 39, 28, 0, 0, 1073742096};
RogueRuntimeType TypeRogueCommonPrimitiveMethodsxRogueInt64x = { "CommonPrimitiveMethods<<Rogue::Int64>>", 0, 40, 29, 0, 0, 1073742096};
RogueRuntimeType TypeRogueAugment_9_Rogue_ByteList = { "Augment_9_Rogue_Byte[]", 0, 41, 30, 0, 0, 1140850960};
RogueRuntimeType TypeRogueAugment_24_Rogue_ByteList = { "Augment_24_Rogue_Byte[]", 0, 42, 31, 0, 0, 1140850960};
RogueRuntimeType TypeRogueCommonPrimitiveMethodsxRogueBytex = { "CommonPrimitiveMethods<<Rogue::Byte>>", 0, 43, 32, 0, 0, 1073742096};
RogueRuntimeType TypeRogueCommonPrimitiveMethodsxRogueReal32x = { "CommonPrimitiveMethods<<Rogue::Real32>>", 0, 44, 33, 0, 0, 1073742096};
RogueRuntimeType TypeRogueCommonPrimitiveMethodsxRogueReal64x = { "CommonPrimitiveMethods<<Rogue::Real64>>", 0, 45, 34, 0, 0, 1073742096};
RogueRuntimeType TypeRogueAugment_10_Rogue_StringList = { "Augment_10_Rogue_String[]", 0, 46, 35, 0, 0, 1140850960};
RogueRuntimeType TypeRogueOptionalType = { "OptionalType", 0, 47, 36, 0, 0, 1073742096};
RogueRuntimeType TypeRogueOptionalAugment_6_Rogue_Int32 = { "Augment_6_Rogue_Int32?", 0, 48, 37, 0, 0, 1140850960};
RogueRuntimeType TypeRogueReaderxRogueCharacterx = { "Reader<<Rogue::Character>>", 0, 49, 38, 0, 0, 1090519312};
RogueRuntimeType TypeRogueBufferedPrintWriterxglobal_output_bufferx = { "BufferedPrintWriter<<global_output_buffer>>", 0, 50, 42, 0, 0, 1090584848};

void* TypeRogueSystem_vtable[3] = {(void*)RogueObject__toxRogueStringx,(void*)RogueSystem__init_object,(void*)RogueSystem__type_name};

RogueRuntimeType TypeRogueSystem = { "System", 0, 51, 45, 0, 0, 1073742152, TypeRogueSystem_vtable, 0, 0, 0, sizeof(RogueSystem), Rogue_base_types+34, 1, 0, (RogueFn_Object)RogueSystem__init_object, 0, (RogueFn_Object)RogueObject_gc_trace, 0, 0 };
RogueStringList* RogueSystem__g_command_line_arguments = 0;
RogueString* RogueSystem__g_executable_filepath = 0;
RogueInt64 RogueSystem__g_execution_start_ms = 0;

void* TypeRogueConsoleMode_vtable[7] = {(void*)RogueObject__toxRogueStringx,(void*)RogueConsoleMode__init_object,(void*)RogueConsoleMode__type_name,0,0,(void*)RogueConsoleMode___on_exit,(void*)RogueConsoleMode___on_enter};

RogueRuntimeType TypeRogueConsoleMode = { "ConsoleMode", 0, 52, 50, 0, 0, 1090519304, TypeRogueConsoleMode_vtable, 0, 0, 0, sizeof(RogueConsoleMode), Rogue_base_types+41, 1, 0, (RogueFn_Object)RogueConsoleMode__init_object, 0, (RogueFn_Object)RogueObject_gc_trace, 0, 0 };
RogueLogical RogueConsoleMode__g_configured_on_exit = 0;

void* TypeRogueConsole_vtable[20] = {(void*)RogueConsole__toxRogueStringx,(void*)RogueConsole__init_object,(void*)RogueConsole__type_name,(void*)RogueConsole__init,(void*)RogueConsole__error,(void*)RogueConsole__flush__RogueString,(void*)RogueConsole__has_another,(void*)RogueConsole__mode,(void*)RogueConsole__read,(void*)RogueConsole__set_immediate_mode__RogueLogical,(void*)RogueConsole__width,(void*)RogueConsole__write__RogueString,(void*)RogueConsole___fill_input_buffer__RogueInt32,(void*)RogueConsole__flush,(void*)RogueConsole__print__RogueCharacter,(void*)RogueConsole__print__RogueObject,(void*)RogueConsole__print__RogueString,(void*)RogueConsole__println,(void*)RogueConsole__println__RogueObject,(void*)RogueConsole__println__RogueString};

RogueRuntimeType TypeRogueConsole = { "Console", 0, 53, 46, 0, 0, 1075904904, TypeRogueConsole_vtable, 0, 0, 0, sizeof(RogueConsole), Rogue_base_types+35, 4, 0, (RogueFn_Object)RogueConsole__init_object, (RogueFn_Object)RogueConsole__init, (RogueFn_Object)RogueConsole_gc_trace, 0, 0 };
RogueConsole* RogueConsole_singleton = 0;

void* TypeRogueObjectPoolxRogueStringx_vtable[5] = {(void*)RogueObject__toxRogueStringx,(void*)RogueObjectPoolxRogueStringx__init_object,(void*)RogueObjectPoolxRogueStringx__type_name,(void*)RogueObjectPoolxRogueStringx__on_use,(void*)RogueObjectPoolxRogueStringx__on_end_use__RogueString};

RogueRuntimeType TypeRogueObjectPoolxRogueStringx = { "ObjectPool<<Rogue::String>>", 0, 54, 47, 0, 0, 1075904904, TypeRogueObjectPoolxRogueStringx_vtable, 0, 0, 0, sizeof(RogueObjectPoolxRogueStringx), Rogue_base_types+39, 1, 0, (RogueFn_Object)RogueObjectPoolxRogueStringx__init_object, 0, (RogueFn_Object)RogueObjectPoolxRogueStringx_gc_trace, 0, 0 };
RogueObjectPoolxRogueStringx* RogueObjectPoolxRogueStringx_singleton = 0;
RogueRuntimeType TypeRogueBufferedPrintWriterxoutput_bufferx = { "BufferedPrintWriter<<output_buffer>>", 0, 55, 48, 0, 0, 1090584848};

void* TypeRogueConsoleEventTypeList_vtable[13] = {(void*)RogueConsoleEventTypeList__toxRogueStringx,(void*)RogueConsoleEventTypeList__init_object,(void*)RogueConsoleEventTypeList__type_name,(void*)RogueConsoleEventTypeList__init__RogueInt32,(void*)RogueConsoleEventTypeList__on_cleanup,(void*)RogueConsoleEventTypeList__add__RogueConsoleEventType,(void*)RogueConsoleEventTypeList__clear,(void*)RogueConsoleEventTypeList__description,(void*)RogueConsoleEventTypeList__discard_from__RogueInt32,(void*)RogueConsoleEventTypeList__get__RogueInt32,(void*)RogueConsoleEventTypeList__on_return_to_pool,(void*)RogueConsoleEventTypeList__reserve__RogueInt32,(void*)RogueConsoleEventTypeList__zero__RogueInt32_RogueOptionalInt32};

RogueRuntimeType TypeRogueConsoleEventTypeList = { "ConsoleEventType[]", 0, 56, 53, 0, 0, 1073807624, TypeRogueConsoleEventTypeList_vtable, 0, 0, 0, sizeof(RogueConsoleEventTypeList), Rogue_base_types+42, 3, 0, (RogueFn_Object)RogueConsoleEventTypeList__init_object, 0, (RogueFn_Object)RogueConsoleEventTypeList_gc_trace, (RogueFn_Object)RogueConsoleEventTypeList__on_cleanup, 0 };

void* TypeRogueConsoleErrorPrinter_vtable[12] = {(void*)RogueObject__toxRogueStringx,(void*)RogueConsoleErrorPrinter__init_object,(void*)RogueConsoleErrorPrinter__type_name,(void*)RogueConsoleErrorPrinter__flush__RogueString,(void*)RogueConsoleErrorPrinter__write__RogueString,(void*)RogueConsoleErrorPrinter__flush,(void*)RogueConsoleErrorPrinter__print__RogueCharacter,(void*)RogueConsoleErrorPrinter__print__RogueObject,(void*)RogueConsoleErrorPrinter__print__RogueString,(void*)RogueConsoleErrorPrinter__println,(void*)RogueConsoleErrorPrinter__println__RogueObject,(void*)RogueConsoleErrorPrinter__println__RogueString};

RogueRuntimeType TypeRogueConsoleErrorPrinter = { "ConsoleErrorPrinter", 0, 57, 54, 0, 0, 1073807624, TypeRogueConsoleErrorPrinter_vtable, 0, 0, 0, sizeof(RogueConsoleErrorPrinter), Rogue_base_types+45, 3, 0, (RogueFn_Object)RogueConsoleErrorPrinter__init_object, 0, (RogueFn_Object)RogueConsoleErrorPrinter_gc_trace, 0, 0 };

void* TypeRogueFunction_260_vtable[4] = {(void*)RogueObject__toxRogueStringx,(void*)RogueFunction_260__init_object,(void*)RogueFunction_260__type_name,(void*)RogueFunction_260__call};

RogueRuntimeType TypeRogueFunction_260 = { "Function_260", 0, 58, 57, 0, 0, 1075839368, TypeRogueFunction_260_vtable, 0, 0, 0, sizeof(RogueFunction_260), Rogue_base_types+48, 2, 0, (RogueFn_Object)RogueFunction_260__init_object, 0, (RogueFn_Object)RogueObject_gc_trace, 0, 0 };
RogueFunction_260* RogueFunction_260_singleton = 0;

void* TypeRogueStandardConsoleMode_vtable[8] = {(void*)RogueObject__toxRogueStringx,(void*)RogueStandardConsoleMode__init_object,(void*)RogueStandardConsoleMode__type_name,(void*)RogueStandardConsoleMode__has_another,(void*)RogueStandardConsoleMode__read,(void*)RogueConsoleMode___on_exit,(void*)RogueConsoleMode___on_enter,(void*)RogueStandardConsoleMode__init};

RogueRuntimeType TypeRogueStandardConsoleMode = { "StandardConsoleMode", 0, 59, 58, 0, 0, 1073742088, TypeRogueStandardConsoleMode_vtable, 0, 0, 0, sizeof(RogueStandardConsoleMode), Rogue_base_types+50, 2, 0, (RogueFn_Object)RogueStandardConsoleMode__init_object, (RogueFn_Object)RogueStandardConsoleMode__init, (RogueFn_Object)RogueObject_gc_trace, 0, 0 };

void* TypeRogueFunction_262_vtable[4] = {(void*)RogueObject__toxRogueStringx,(void*)RogueFunction_262__init_object,(void*)RogueFunction_262__type_name,(void*)RogueFunction_262__call};

RogueRuntimeType TypeRogueFunction_262 = { "Function_262", 0, 60, 59, 0, 0, 1075839368, TypeRogueFunction_262_vtable, 0, 0, 0, sizeof(RogueFunction_262), Rogue_base_types+52, 2, 0, (RogueFn_Object)RogueFunction_262__init_object, 0, (RogueFn_Object)RogueObject_gc_trace, 0, 0 };
RogueFunction_262* RogueFunction_262_singleton = 0;

void* TypeRogueConsoleEventList_vtable[18] = {(void*)RogueConsoleEventList__toxRogueStringx,(void*)RogueConsoleEventList__init_object,(void*)RogueConsoleEventList__type_name,(void*)RogueConsoleEventList__init,(void*)RogueConsoleEventList__on_cleanup,(void*)RogueConsoleEventList__add__RogueConsoleEvent,(void*)RogueConsoleEventList__clear,(void*)RogueConsoleEventList__copy__RogueInt32_RogueInt32_RogueConsoleEventList_RogueInt32,(void*)RogueConsoleEventList__description,(void*)RogueConsoleEventList__discard_from__RogueInt32,(void*)RogueConsoleEventList__ensure_capacity__RogueInt32,(void*)RogueConsoleEventList__expand_to_count__RogueInt32,(void*)RogueConsoleEventList__get__RogueInt32,(void*)RogueConsoleEventList__on_return_to_pool,(void*)RogueConsoleEventList__remove_first,(void*)RogueConsoleEventList__reserve__RogueInt32,(void*)RogueConsoleEventList__shift__RogueInt32,(void*)RogueConsoleEventList__zero__RogueInt32_RogueOptionalInt32};

RogueRuntimeType TypeRogueConsoleEventList = { "ConsoleEvent[]", 0, 61, 61, 0, 0, 1073742088, TypeRogueConsoleEventList_vtable, 0, 0, 0, sizeof(RogueConsoleEventList), Rogue_base_types+56, 3, 0, (RogueFn_Object)RogueConsoleEventList__init_object, (RogueFn_Object)RogueConsoleEventList__init, (RogueFn_Object)RogueObject_gc_trace, (RogueFn_Object)RogueConsoleEventList__on_cleanup, 0 };

void* TypeRogueImmediateConsoleMode_vtable[14] = {(void*)RogueObject__toxRogueStringx,(void*)RogueImmediateConsoleMode__init_object,(void*)RogueImmediateConsoleMode__type_name,(void*)RogueImmediateConsoleMode__has_another,(void*)RogueImmediateConsoleMode__read,(void*)RogueImmediateConsoleMode___on_exit,(void*)RogueConsoleMode___on_enter,(void*)RogueImmediateConsoleMode__init,(void*)RogueImmediateConsoleMode___fill_event_queue,(void*)RogueImmediateConsoleMode___fill_event_queue_windows,(void*)RogueImmediateConsoleMode___fill_event_queue_windows_process_next,(void*)RogueImmediateConsoleMode___fill_event_queue_windows_process_next__RogueWindowsInputRecord,(void*)RogueImmediateConsoleMode___fill_event_queue_unix,(void*)RogueImmediateConsoleMode___fill_event_queue_unix_process_next};

RogueRuntimeType TypeRogueImmediateConsoleMode = { "ImmediateConsoleMode", 0, 62, 60, 0, 0, 1073807624, TypeRogueImmediateConsoleMode_vtable, 0, 0, 0, sizeof(RogueImmediateConsoleMode), Rogue_base_types+54, 2, 0, (RogueFn_Object)RogueImmediateConsoleMode__init_object, (RogueFn_Object)RogueImmediateConsoleMode__init, (RogueFn_Object)RogueImmediateConsoleMode_gc_trace, 0, 0 };

void* TypeRogueUnixConsoleMouseEventTypeList_vtable[13] = {(void*)RogueUnixConsoleMouseEventTypeList__toxRogueStringx,(void*)RogueUnixConsoleMouseEventTypeList__init_object,(void*)RogueUnixConsoleMouseEventTypeList__type_name,(void*)RogueUnixConsoleMouseEventTypeList__init__RogueInt32,(void*)RogueUnixConsoleMouseEventTypeList__on_cleanup,(void*)RogueUnixConsoleMouseEventTypeList__add__RogueUnixConsoleMouseEventType,(void*)RogueUnixConsoleMouseEventTypeList__clear,(void*)RogueUnixConsoleMouseEventTypeList__description,(void*)RogueUnixConsoleMouseEventTypeList__discard_from__RogueInt32,(void*)RogueUnixConsoleMouseEventTypeList__get__RogueInt32,(void*)RogueUnixConsoleMouseEventTypeList__on_return_to_pool,(void*)RogueUnixConsoleMouseEventTypeList__reserve__RogueInt32,(void*)RogueUnixConsoleMouseEventTypeList__zero__RogueInt32_RogueOptionalInt32};

RogueRuntimeType TypeRogueUnixConsoleMouseEventTypeList = { "UnixConsoleMouseEventType[]", 0, 63, 64, 0, 0, 1073807624, TypeRogueUnixConsoleMouseEventTypeList_vtable, 0, 0, 0, sizeof(RogueUnixConsoleMouseEventTypeList), Rogue_base_types+59, 3, 0, (RogueFn_Object)RogueUnixConsoleMouseEventTypeList__init_object, 0, (RogueFn_Object)RogueUnixConsoleMouseEventTypeList_gc_trace, (RogueFn_Object)RogueUnixConsoleMouseEventTypeList__on_cleanup, 0 };
RogueInt32 Rogue_string_table_count = 0;

RogueString* RogueString_create( const char* cstring )
{
  return RogueString_create_from_utf8( cstring, -1, ROGUE_STRING_COPY );
}

RogueString* RogueString_create_from_utf8( const char* cstring, int byte_count, int usage )
{
  // Creates an immutable String object.
  //
  // usage
  //   COPY           - Make a copy of 'cstring'.
  //   BORROW         - Use externally-malloc'd 'cstring' but don't free it on GC.
  //   ADOPT          - Use externally-malloc'd 'cstring' and free() it on GC.
  //   PERMANENT      - Like BORROW but new String is not tracked by the GC.
  //   PERMANENT_COPY - Like COPY but new String is not tracked by the GC.
  if (byte_count == -1) byte_count = (int) strlen(cstring);
  RogueInt32 character_count = RogueString_utf8_character_count( cstring, byte_count );
  if (character_count == -1) return RogueString_create_from_ascii256( cstring, byte_count, usage );

  int is_permanent = (usage == ROGUE_STRING_PERMANENT) || (usage == ROGUE_STRING_PERMANENT_COPY);
  RogueString* result = is_permanent ? ROGUE_NEW_OBJECT(RogueString)   : ROGUE_CREATE_OBJECT(RogueString);
  RogueByteList* data;
  #ifdef ROGUE_GC_AUTO
    ++result->__refcount;
    data = is_permanent ? ROGUE_NEW_OBJECT(RogueByteList) : ROGUE_CREATE_OBJECT(RogueByteList);
    --result->__refcount;
  #else
    data = is_permanent ? ROGUE_NEW_OBJECT(RogueByteList) : ROGUE_CREATE_OBJECT(RogueByteList);
  #endif
  result->data = data;

  if (usage == ROGUE_STRING_COPY || usage == ROGUE_STRING_PERMANENT_COPY)
  {
    char* string_data = (char*)ROGUE_MALLOC( byte_count + 1 );
    memcpy( string_data, cstring, byte_count );
    string_data[byte_count] = 0;
    cstring = string_data;
  }

  data->as_utf8        = (char*) cstring;
  data->count          = byte_count;
  data->capacity       = byte_count + 1;
  data->is_borrowed    = (usage == ROGUE_STRING_BORROW || is_permanent);
  data->element_size   = 1;

  result->count        = character_count;
  result->is_immutable = 1;
  result->hash_code    = -1;
  result->is_ascii     = (character_count == byte_count);
  return result;
}

RogueString* RogueString_create_from_ascii256( const char* cstring, int byte_count, int usage )
{
  if (byte_count == -1) byte_count = (int) strlen(cstring);

  int utf8_byte_count = 0;
  int i;
  for (i=byte_count; --i>=0; )
  {
    if (cstring[i] & 0x80) utf8_byte_count += 2;
    else                   ++utf8_byte_count;
  }

  if (utf8_byte_count == byte_count)
  {
    return RogueString_create_from_utf8( cstring, byte_count, usage );
  }

  int is_permanent = (usage == ROGUE_STRING_PERMANENT) || (usage == ROGUE_STRING_PERMANENT_COPY);
  RogueString* result = is_permanent ? ROGUE_NEW_OBJECT(RogueString) : ROGUE_CREATE_OBJECT(RogueString);
  RogueByteList* data;
  #ifdef ROGUE_GC_AUTO
    ++result->__refcount;
    data = is_permanent ? ROGUE_NEW_OBJECT(RogueByteList) : ROGUE_CREATE_OBJECT(RogueByteList);
    --result->__refcount;
  #else
    data = is_permanent ? ROGUE_NEW_OBJECT(RogueByteList) : ROGUE_CREATE_OBJECT(RogueByteList);
  #endif
  result->data = data;

  data->as_utf8        = (char*) ROGUE_MALLOC( utf8_byte_count+1 );
  data->count          = utf8_byte_count;
  data->capacity       = utf8_byte_count + 1;
  data->is_borrowed    = 0;
  data->element_size   = 1;

  result->count        = byte_count;   // ASCII 256 bytes == characters
  result->is_immutable = 1;
  result->hash_code    = -1;

  char* utf8 = data->as_utf8;
  int dest_i = 0;
  for (i=-1; ++i<byte_count; )
  {
    char ch = cstring[i];
    if (ch & 0x80)
    {
      // %aaaaaaaa ->
      // %110x xxaa 10aa aaaa
      utf8[dest_i]   = 0xC0 | ((ch >> 6) & 0x03);
      utf8[dest_i+1] = 0x80 | ((ch >> 2) & 0x3F);
      dest_i += 2;
    }
    else
    {
      utf8[dest_i++] = ch;
    }
  }
  utf8[ utf8_byte_count ] = 0;

  // We've had to allocate a new buffer so free the original if it needs it.
  if (usage == ROGUE_STRING_ADOPT) ROGUE_FREE( (void*)cstring );

  return result;
}

RogueString* RogueString_create_permanent( const char* cstring )
{
  return RogueString_create_from_utf8( cstring, -1, ROGUE_STRING_PERMANENT );
}

RogueString* RogueString_create_string_table_entry( const char* cstring )
{
  RogueString* result = RogueString_create_permanent( cstring );
  Rogue_string_table[ Rogue_string_table_count++ ] = result;
  return result;
}

RogueInt32 RogueString_compute_hash_code( RogueString* THISOBJ, RogueInt32 starting_hash )
{
  RogueInt32 hash = starting_hash;
  int n = THISOBJ->data->count;
  char* src = THISOBJ->data->as_utf8 - 1;
  while (--n >= 0)
  {
    hash = ((hash<<3) - hash) + *(++src);
  }
  return hash;
}

const char* RogueString_to_c_string( RogueString* st )
{
  if ( !st ) { return "null"; }
  return st->data->as_utf8;
}

RogueInt32 RogueString_utf8_character_count( const char* cstring, int byte_count )
{
  // Returns -1 if 'cstring' is not a valid UTF-8 string.
  if (byte_count == -1) byte_count = (int) strlen(cstring);

  RogueInt32 character_count = 0;
  int i;
  for (i=0; i<byte_count; ++character_count)
  {
    int b = cstring[ i ];
    if (b & 0x80)
    {
      if ( !(b & 0x40) ) { return -1;}

      if (b & 0x20)
      {
        if (b & 0x10)
        {
          // %11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
          if (b & 0x08) { return -1;}
          if (i + 4 > byte_count || ((cstring[i+1] & 0xC0) != 0x80) || ((cstring[i+2] & 0xC0) != 0x80)
              || ((cstring[i+3] & 0xC0) != 0x80)) { return -1;}
          i += 4;
        }
        else
        {
          // %1110xxxx 10xxxxxx 10xxxxxx
          if (i + 3 > byte_count || ((cstring[i+1] & 0xC0) != 0x80) || ((cstring[i+2] & 0xC0) != 0x80))
          {
            return -1;
          }
          i += 3;
        }
      }
      else
      {
        // %110x xxxx 10xx xxxx
        if (i + 2 > byte_count || ((cstring[i+1] & 0xC0) != 0x80)) { return -1; }
        i += 2;
      }
    }
    else
    {
      ++i;
    }
  }

  return character_count;
}


int    Rogue_argc = 0;
char** Rogue_argv = 0;

void Rogue_fwrite( const char* utf8, int byte_count, int out )
{
  #if !defined(ROGUE_PLATFORM_EMBEDDED)
  while (byte_count)
  {
    int n = (int) write( out, utf8, byte_count );
    if (n > 0)
    {
      utf8 += n;
      byte_count -= n;
    }
  }
  #endif
}


RogueInt32 RogueInt32__abs( RogueInt32 THISOBJ )
{
  {
    if (THISOBJ >= 0)
    {
      {
        RogueInt32 _auto_result_0 = THISOBJ;
        return _auto_result_0;
      }
    }
    else
    {
      {
        {
          {
            return -THISOBJ;
          }
        }
      }
    }
  }
}

RogueInt32 RogueInt32__clamped_low__RogueInt32( RogueInt32 THISOBJ, RogueInt32 low_0 )
{
  {
    if (THISOBJ < low_0)
    {
      {
        return low_0;
      }
    }
    RogueInt32 _auto_result_0 = THISOBJ;
    return _auto_result_0;
  }
}

RogueInt32 RogueInt32__digit_count( RogueInt32 THISOBJ )
{
  RogueStringPool* _auto_resource_0_0 = 0;
  (void)_auto_resource_0_0;
  RogueString* builder_1 = 0;
  (void)builder_1;
  RogueInt32 _auto_result_1_2 = 0;
  (void)_auto_result_1_2;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    _auto_resource_0_0 = 0;
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_resource_0_0 );
    builder_1 = 0;
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &builder_1 );
    {
      _auto_resource_0_0 = ROGUE_SINGLETON(RogueStringPool);
      builder_1 = RogueObjectPoolxRogueStringx__on_use(((RogueObjectPoolxRogueStringx*)_auto_resource_0_0));
      RogueString__print__RogueInt32( builder_1, THISOBJ );
      _auto_result_1_2 = RogueString__count(builder_1);
      RogueObjectPoolxRogueStringx__on_end_use__RogueString( ((RogueObjectPoolxRogueStringx*)_auto_resource_0_0), builder_1 );
      TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
      return _auto_result_1_2;
    }
  }
}

RogueInt32 RogueInt32__or_larger__RogueInt32( RogueInt32 THISOBJ, RogueInt32 other_0 )
{
  {
    RogueInt32 _auto_result_0 = ((THISOBJ >= other_0) ? THISOBJ : other_0);
    return _auto_result_0;
  }
}

RogueInt64 RogueInt64__operatorMOD__RogueInt64( RogueInt64 THISOBJ, RogueInt64 other_0 )
{
  RogueInt64 r_1 = 0;
  (void)r_1;

  {
    if (((!THISOBJ) && (!other_0)) || (other_0 == 1))
    {
      {
        RogueInt64 _auto_result_0 = ((RogueInt64)0);
        return _auto_result_0;
      }
    }
    r_1 = THISOBJ % other_0;
    if ((THISOBJ ^ other_0) < ((RogueInt64)0))
    {
      {
        if (!!r_1)
        {
          {
            return (r_1 + other_0);
          }
        }
        else
        {
          {
            {
              {
                RogueInt64 _auto_result_1 = ((RogueInt64)0);
                return _auto_result_1;
              }
            }
          }
        }
      }
    }
    else
    {
      {
        {
          {
            return r_1;
          }
        }
      }
    }
  }
}

void RogueStackTraceFrame_gc_trace( void* THISOBJ )
{

  RogueObject* ref;
  if ((ref = (RogueObject*)((RogueStackTraceFrame*)THISOBJ)->procedure) && ref->__refcount >= 0) ref->__type->fn_gc_trace(ref);
  if ((ref = (RogueObject*)((RogueStackTraceFrame*)THISOBJ)->filename) && ref->__refcount >= 0) ref->__type->fn_gc_trace(ref);
}

RogueString* RogueStackTraceFrame__toxRogueStringx( RogueStackTraceFrame THISOBJ )
{
  RogueString* _auto_context_block_0_0 = 0;
  (void)_auto_context_block_0_0;
  RogueString* _auto_anchored_arg_0_0_1 = 0;
  (void)_auto_anchored_arg_0_0_1;
  RogueString* _auto_anchored_arg_0_1_2 = 0;
  (void)_auto_anchored_arg_0_1_2;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_context_block_0_0 );
    _auto_context_block_0_0 = ROGUE_CREATE_OBJECT( RogueString );
    RogueString__init(_auto_context_block_0_0);
    RogueString__print__RogueString( _auto_context_block_0_0, str__ );
    _auto_anchored_arg_0_0_1 = 0;
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_arg_0_0_1 );
    RogueString__print__RogueString( _auto_context_block_0_0, (_auto_anchored_arg_0_0_1=THISOBJ.procedure) );
    RogueString__print__RogueString( _auto_context_block_0_0, str___ );
    _auto_anchored_arg_0_1_2 = 0;
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_arg_0_1_2 );
    RogueString__print__RogueString( _auto_context_block_0_0, (_auto_anchored_arg_0_1_2=THISOBJ.filename) );
    RogueString__print__RogueString( _auto_context_block_0_0, str___1 );
    RogueString__print__RogueInt32( _auto_context_block_0_0, THISOBJ.line );
    RogueString__print__RogueString( _auto_context_block_0_0, str___2 );
    TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
    return _auto_context_block_0_0;
  }
}

RogueString* RogueStackTraceFrame__toxRogueStringx__RogueInt32_RogueInt32( RogueStackTraceFrame THISOBJ, RogueInt32 left_w_0, RogueInt32 right_w_1 )
{
  RogueString* builder_2 = 0;
  (void)builder_2;
  RogueString* _auto_anchored_context_0_3 = 0;
  (void)_auto_anchored_context_0_3;
  RogueString* _auto_anchored_context_1_4 = 0;
  (void)_auto_anchored_context_1_4;
  RogueString* _auto_anchored_arg_0_2_5 = 0;
  (void)_auto_anchored_arg_0_2_5;
  RogueString* _auto_anchored_arg_0_3_6 = 0;
  (void)_auto_anchored_arg_0_3_6;
  RogueString* _auto_anchored_context_4_7 = 0;
  (void)_auto_anchored_context_4_7;
  RogueString* _auto_anchored_arg_0_5_8 = 0;
  (void)_auto_anchored_arg_0_5_8;
  RogueString* _auto_anchored_context_6_9 = 0;
  (void)_auto_anchored_context_6_9;
  RogueString* _auto_anchored_context_7_10 = 0;
  (void)_auto_anchored_context_7_10;
  RogueString* _auto_anchored_arg_0_8_11 = 0;
  (void)_auto_anchored_arg_0_8_11;
  RogueString* _auto_anchored_arg_0_9_12 = 0;
  (void)_auto_anchored_arg_0_9_12;
  RogueString* _auto_anchored_context_10_13 = 0;
  (void)_auto_anchored_context_10_13;
  RogueString* _auto_anchored_arg_0_11_14 = 0;
  (void)_auto_anchored_arg_0_11_14;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &builder_2 );
    builder_2 = ROGUE_CREATE_OBJECT( RogueString );
    RogueString__init(builder_2);
    RogueString__print__RogueCharacter( builder_2, '[' );
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_0_3 );
    if (RogueString__count((_auto_anchored_context_0_3=THISOBJ.procedure)) > left_w_0)
    {
      {
        RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_1_4 );
        _auto_anchored_arg_0_2_5 = 0;
        RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_arg_0_2_5 );
        _auto_anchored_arg_0_3_6 = 0;
        RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_arg_0_3_6 );
        RogueString__print__RogueString( builder_2, (_auto_anchored_arg_0_3_6=RogueString__operatorPLUS__RogueString_RogueString( (_auto_anchored_arg_0_2_5=RogueString__leftmost__RogueInt32( (_auto_anchored_context_1_4=THISOBJ.procedure), left_w_0 - 3 )), str____ )) );
      }
    }
    else
    {
      {
        {
          {
            RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_4_7 );
            _auto_anchored_arg_0_5_8 = 0;
            RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_arg_0_5_8 );
            RogueString__print__RogueString( builder_2, (_auto_anchored_arg_0_5_8=RogueString__justified__RogueInt32_RogueCharacter( (_auto_anchored_context_4_7=THISOBJ.procedure), -left_w_0, ' ' )) );
          }
        }
      }
    }
    RogueString__print__RogueString( builder_2, str___ );
    (right_w_1 -= RogueInt32__digit_count(THISOBJ.line));
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_6_9 );
    if (RogueString__count((_auto_anchored_context_6_9=THISOBJ.filename)) > right_w_1)
    {
      {
        RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_7_10 );
        _auto_anchored_arg_0_8_11 = 0;
        RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_arg_0_8_11 );
        _auto_anchored_arg_0_9_12 = 0;
        RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_arg_0_9_12 );
        RogueString__print__RogueString( builder_2, (_auto_anchored_arg_0_9_12=RogueString__operatorPLUS__RogueString_RogueString( (_auto_anchored_arg_0_8_11=RogueString__leftmost__RogueInt32( (_auto_anchored_context_7_10=THISOBJ.filename), right_w_1 - 3 )), str____ )) );
      }
    }
    else
    {
      {
        {
          {
            RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_10_13 );
            _auto_anchored_arg_0_11_14 = 0;
            RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_arg_0_11_14 );
            RogueString__print__RogueString( builder_2, (_auto_anchored_arg_0_11_14=RogueString__justified__RogueInt32_RogueCharacter( (_auto_anchored_context_10_13=THISOBJ.filename), right_w_1, ' ' )) );
          }
        }
      }
    }
    RogueString__print__RogueCharacter( builder_2, ':' );
    RogueString__print__RogueInt32( builder_2, THISOBJ.line );
    RogueString__print__RogueCharacter( builder_2, ']' );
    TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
    return builder_2;
  }
}

void RogueConsoleCursor__init_class(void)
{
  {
    RogueConsoleCursor__g_cursor_hidden = 0;
  }
}

void RogueConsoleCursor__hide__RogueLogical_RoguePrintWriter( RogueConsoleCursor THISOBJ, RogueLogical setting_0, RogueObject* output_1 )
{
  RogueObject* _auto_context_block_0_2 = 0;
  (void)_auto_context_block_0_2;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    if (RogueConsoleCursor__g_cursor_hidden == setting_0)
    {
      {
        TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
        return;
      }
    }
    RogueConsoleCursor__g_cursor_hidden = setting_0;
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_context_block_0_2 );
    _auto_context_block_0_2 = output_1;
    Rogue_dispatch_flush_(_auto_context_block_0_2);
    Rogue_dispatch_print__RogueString( _auto_context_block_0_2, str____25 );
    Rogue_dispatch_print__RogueCharacter( _auto_context_block_0_2, (setting_0 ? 'l' : 'h') );
    Rogue_dispatch_flush_(_auto_context_block_0_2);
  }
  TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
}

void RogueConsoleCursor__show__RoguePrintWriter( RogueConsoleCursor THISOBJ, RogueObject* output_0 )
{
  {
    RogueConsoleCursor__hide__RogueLogical_RoguePrintWriter( THISOBJ, 0, output_0 );
  }
}

void RogueConsoleEventType__init_class(void)
{
  RogueConsoleEventTypeList* _auto_context_block_0_0 = 0;
  (void)_auto_context_block_0_0;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_context_block_0_0 );
    _auto_context_block_0_0 = ROGUE_CREATE_OBJECT( RogueConsoleEventTypeList );
    RogueConsoleEventTypeList__init__RogueInt32( _auto_context_block_0_0, 7 );
    RogueConsoleEventTypeList__add__RogueConsoleEventType( _auto_context_block_0_0, (RogueConsoleEventType) {0} );
    RogueConsoleEventTypeList__add__RogueConsoleEventType( _auto_context_block_0_0, (RogueConsoleEventType) {1} );
    RogueConsoleEventTypeList__add__RogueConsoleEventType( _auto_context_block_0_0, (RogueConsoleEventType) {2} );
    RogueConsoleEventTypeList__add__RogueConsoleEventType( _auto_context_block_0_0, (RogueConsoleEventType) {3} );
    RogueConsoleEventTypeList__add__RogueConsoleEventType( _auto_context_block_0_0, (RogueConsoleEventType) {4} );
    RogueConsoleEventTypeList__add__RogueConsoleEventType( _auto_context_block_0_0, (RogueConsoleEventType) {5} );
    RogueConsoleEventTypeList__add__RogueConsoleEventType( _auto_context_block_0_0, (RogueConsoleEventType) {6} );
    RogueConsoleEventType__g_categories = _auto_context_block_0_0;
  }
  TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
}

RogueLogical RogueConsoleEventType__operator____RogueConsoleEventType( RogueConsoleEventType THISOBJ, RogueConsoleEventType other_0 )
{
  {
    return THISOBJ.value == other_0.value;
  }
}

RogueString* RogueConsoleEventType__toxRogueStringx( RogueConsoleEventType THISOBJ )
{
  RogueString* _auto_context_block_0_0 = 0;
  (void)_auto_context_block_0_0;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    switch (THISOBJ.value)
    {
      case 0:
      {
        {
          TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
          return str_CHARACTER;
        }
      }
      case 1:
      {
        {
          TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
          return str_POINTER_PRESS_LEFT;
        }
      }
      case 2:
      {
        {
          TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
          return str_POINTER_PRESS_RIGHT;
        }
      }
      case 3:
      {
        {
          TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
          return str_POINTER_RELEASE;
        }
      }
      case 4:
      {
        {
          TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
          return str_POINTER_MOVE;
        }
      }
      case 5:
      {
        {
          TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
          return str_SCROLL_UP;
        }
      }
      case 6:
      {
        {
          TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
          return str_SCROLL_DOWN;
        }
      }
      default:
      {
        {
          RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_context_block_0_0 );
          _auto_context_block_0_0 = ROGUE_CREATE_OBJECT( RogueString );
          RogueString__init(_auto_context_block_0_0);
          RogueString__print__RogueString( _auto_context_block_0_0, str_ConsoleEventType_ );
          RogueString__print__RogueInt32( _auto_context_block_0_0, THISOBJ.value );
          RogueString__print__RogueString( _auto_context_block_0_0, str___5 );
          TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
          return _auto_context_block_0_0;
        }
      }
    }
  }
}

RogueLogical RogueConsoleEvent__is_character( RogueConsoleEvent THISOBJ )
{
  {
    RogueLogical _auto_result_0 = RogueConsoleEventType__operator____RogueConsoleEventType( THISOBJ.type, (RogueConsoleEventType) {0} );
    return _auto_result_0;
  }
}

RogueString* RogueConsoleEvent__toxRogueStringx( RogueConsoleEvent THISOBJ )
{
  RogueString* _auto_context_block_0_0 = 0;
  (void)_auto_context_block_0_0;
  RogueString* _auto_anchored_arg_0_0_1 = 0;
  (void)_auto_anchored_arg_0_0_1;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    switch (THISOBJ.type.value)
    {
      case 0:
      {
        {
          switch (THISOBJ.x)
          {
            case 8:
            {
              {
                TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
                return str_BACKSPACE;
              }
              break;
            }
            case ((RogueCharacter)9):
            {
              {
                TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
                return str_TAB;
              }
              break;
            }
            case 10:
            {
              {
                TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
                return str_NEWLINE;
              }
              break;
            }
            case 27:
            {
              {
                TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
                return str_ESCAPE;
              }
              break;
            }
            case 17:
            {
              {
                TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
                return str_UP;
              }
              break;
            }
            case 18:
            {
              {
                TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
                return str_DOWN;
              }
              break;
            }
            case 19:
            {
              {
                TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
                return str_RIGHT;
              }
              break;
            }
            case 20:
            {
              {
                TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
                return str_LEFT;
              }
              break;
            }
            case 127:
            {
              {
                TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
                return str_DELETE;
              }
              break;
            }
            default:
            {
              {
                RogueString* _auto_result_0 = (RogueString*)RogueString__operatorPLUS__RogueString_RogueCharacter( str_, ((RogueCharacter)THISOBJ.x) );
                TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
                return _auto_result_0;
              }
            }
          }
        }
        break;
      }
      case 5:
      case 6:
      {
        {
          RogueString* _auto_result_1 = (RogueString*)RogueConsoleEventType__toxRogueStringx(THISOBJ.type);
          TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
          return _auto_result_1;
        }
        break;
      }
    }
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_context_block_0_0 );
    _auto_context_block_0_0 = ROGUE_CREATE_OBJECT( RogueString );
    RogueString__init(_auto_context_block_0_0);
    _auto_anchored_arg_0_0_1 = 0;
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_arg_0_0_1 );
    RogueString__print__RogueString( _auto_context_block_0_0, (_auto_anchored_arg_0_0_1=RogueConsoleEventType__toxRogueStringx(THISOBJ.type)) );
    RogueString__print__RogueString( _auto_context_block_0_0, str___3 );
    RogueString__print__RogueInt32( _auto_context_block_0_0, THISOBJ.x );
    RogueString__print__RogueString( _auto_context_block_0_0, str___4 );
    RogueString__print__RogueInt32( _auto_context_block_0_0, THISOBJ.y );
    RogueString__print__RogueString( _auto_context_block_0_0, str___5 );
    TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
    return _auto_context_block_0_0;
  }
}

RogueRangeUpToLessThanIteratorxRogueInt32x RogueRangeUpToLessThanxRogueInt32x__iterator( RogueRangeUpToLessThanxRogueInt32x THISOBJ )
{
  {
    return (RogueRangeUpToLessThanIteratorxRogueInt32x) {THISOBJ.start,THISOBJ.limit,THISOBJ.step};
  }
}

RogueOptionalInt32 RogueRangeUpToLessThanIteratorxRogueInt32x__read_another( RogueRangeUpToLessThanIteratorxRogueInt32x* THISOBJ )
{
  RogueInt32 result_0 = 0;
  (void)result_0;

  {
    if (THISOBJ->cur < THISOBJ->limit)
    {
      {
        result_0 = THISOBJ->cur;
        THISOBJ->cur += THISOBJ->step;
        return (RogueOptionalInt32) {result_0,1};
      }
    }
    else
    {
      {
        {
          {
            return (RogueOptionalInt32){0};
          }
        }
      }
    }
  }
}

void RogueUnixConsoleMouseEventType__init_class(void)
{
  RogueUnixConsoleMouseEventTypeList* _auto_context_block_0_0 = 0;
  (void)_auto_context_block_0_0;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_context_block_0_0 );
    _auto_context_block_0_0 = ROGUE_CREATE_OBJECT( RogueUnixConsoleMouseEventTypeList );
    RogueUnixConsoleMouseEventTypeList__init__RogueInt32( _auto_context_block_0_0, 8 );
    RogueUnixConsoleMouseEventTypeList__add__RogueUnixConsoleMouseEventType( _auto_context_block_0_0, (RogueUnixConsoleMouseEventType) {32} );
    RogueUnixConsoleMouseEventTypeList__add__RogueUnixConsoleMouseEventType( _auto_context_block_0_0, (RogueUnixConsoleMouseEventType) {34} );
    RogueUnixConsoleMouseEventTypeList__add__RogueUnixConsoleMouseEventType( _auto_context_block_0_0, (RogueUnixConsoleMouseEventType) {35} );
    RogueUnixConsoleMouseEventTypeList__add__RogueUnixConsoleMouseEventType( _auto_context_block_0_0, (RogueUnixConsoleMouseEventType) {64} );
    RogueUnixConsoleMouseEventTypeList__add__RogueUnixConsoleMouseEventType( _auto_context_block_0_0, (RogueUnixConsoleMouseEventType) {66} );
    RogueUnixConsoleMouseEventTypeList__add__RogueUnixConsoleMouseEventType( _auto_context_block_0_0, (RogueUnixConsoleMouseEventType) {67} );
    RogueUnixConsoleMouseEventTypeList__add__RogueUnixConsoleMouseEventType( _auto_context_block_0_0, (RogueUnixConsoleMouseEventType) {96} );
    RogueUnixConsoleMouseEventTypeList__add__RogueUnixConsoleMouseEventType( _auto_context_block_0_0, (RogueUnixConsoleMouseEventType) {97} );
    RogueUnixConsoleMouseEventType__g_categories = _auto_context_block_0_0;
  }
  TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
}

RogueString* RogueUnixConsoleMouseEventType__toxRogueStringx( RogueUnixConsoleMouseEventType THISOBJ )
{
  RogueString* _auto_context_block_0_0 = 0;
  (void)_auto_context_block_0_0;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    switch (THISOBJ.value)
    {
      case 32:
      {
        {
          TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
          return str_PRESS_LEFT;
        }
      }
      case 34:
      {
        {
          TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
          return str_PRESS_RIGHT;
        }
      }
      case 35:
      {
        {
          TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
          return str_RELEASE;
        }
      }
      case 64:
      {
        {
          TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
          return str_DRAG_LEFT;
        }
      }
      case 66:
      {
        {
          TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
          return str_DRAG_RIGHT;
        }
      }
      case 67:
      {
        {
          TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
          return str_MOVE;
        }
      }
      case 96:
      {
        {
          TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
          return str_SCROLL_UP;
        }
      }
      case 97:
      {
        {
          TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
          return str_SCROLL_DOWN;
        }
      }
      default:
      {
        {
          RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_context_block_0_0 );
          _auto_context_block_0_0 = ROGUE_CREATE_OBJECT( RogueString );
          RogueString__init(_auto_context_block_0_0);
          RogueString__print__RogueString( _auto_context_block_0_0, str_UnixConsoleMouseEven );
          RogueString__print__RogueInt32( _auto_context_block_0_0, THISOBJ.value );
          RogueString__print__RogueString( _auto_context_block_0_0, str___5 );
          TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
          return _auto_context_block_0_0;
        }
      }
    }
  }
}

void RogueByteList_gc_trace( void* THISOBJ )
{
  ((RogueObject*)THISOBJ)->__refcount = ~((RogueObject*)THISOBJ)->__refcount;
}

void RogueByteList__init( RogueByteList* THISOBJ )
{
}

void RogueByteList__init__RogueInt32( RogueByteList* THISOBJ, RogueInt32 capacity_0 )
{
  {
    RogueByteList__reserve__RogueInt32( THISOBJ, capacity_0 );
  }
}

void RogueByteList__init_object( RogueByteList* THISOBJ )
{
  {
    RogueObject__init_object(((RogueObject*)THISOBJ));
    {
      {
        THISOBJ->element_size = sizeof(THISOBJ->element_type);
      }
    }
  }
}

void RogueByteList__on_cleanup( RogueByteList* THISOBJ )
{
  {
    if (THISOBJ->as_bytes)
    {
      if (THISOBJ->is_borrowed) THISOBJ->is_borrowed = 0;
      else              ROGUE_FREE( THISOBJ->as_bytes );
      THISOBJ->as_bytes = 0;
      THISOBJ->capacity = 0;
      THISOBJ->count = 0;
    }
  }
}

void RogueByteList__add__RogueByte( RogueByteList* THISOBJ, RogueByte value_0 )
{
  {
    RogueByteList__reserve__RogueInt32( THISOBJ, 1 );
    {
      {
        ((RogueByte*)(THISOBJ->data))[THISOBJ->count++] = value_0;
      }
    }
  }
}

void RogueByteList__clear( RogueByteList* THISOBJ )
{
  {
    RogueByteList__discard_from__RogueInt32( THISOBJ, 0 );
  }
}

void RogueByteList__copy__RogueInt32_RogueInt32_RogueByteList_RogueInt32( RogueByteList* THISOBJ, RogueInt32 src_i1_0, RogueInt32 src_count_1, RogueByteList* dest_2, RogueInt32 dest_i1_3 )
{
  {
    if (src_count_1 <= 0)
    {
      {
        return;
      }
    }
    RogueByteList__expand_to_count__RogueInt32( dest_2, (dest_i1_3 + src_count_1) );
    if (THISOBJ == dest_2)
    {
      {
        memmove(
          dest_2->as_bytes + dest_i1_3*THISOBJ->element_size,
          THISOBJ->as_bytes + src_i1_0*THISOBJ->element_size,
          src_count_1 * THISOBJ->element_size
        );
      }
    }
    else
    {
      {
        {
          {
            memcpy(
              dest_2->as_bytes + dest_i1_3*THISOBJ->element_size,
              THISOBJ->as_bytes + src_i1_0*THISOBJ->element_size,
              src_count_1 * THISOBJ->element_size
            );
          }
        }
      }
    }
  }
}

RogueString* RogueByteList__description( RogueByteList* THISOBJ )
{
  RogueString* result_0 = 0;
  (void)result_0;
  RogueInt32 i_1 = 0;
  (void)i_1;
  RogueByteList* _auto_collection_0_2 = 0;
  (void)_auto_collection_0_2;
  RogueInt32 _auto_count_1_3 = 0;
  (void)_auto_count_1_3;
  RogueByte value_4 = 0;
  (void)value_4;
  RogueLogical _auto_condition_0_5 = 0;
  (void)_auto_condition_0_5;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &result_0 );
    result_0 = ROGUE_CREATE_OBJECT( RogueString );
    RogueString__init(result_0);
    RogueString__print__RogueCharacter( result_0, '[' );
    i_1 = 0;
    _auto_collection_0_2 = 0;
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_collection_0_2 );
    _auto_collection_0_2 = THISOBJ;
    _auto_count_1_3 = 0;
    _auto_count_1_3 = _auto_collection_0_2->count;
    value_4 = 0;
    _auto_condition_0_5 = i_1 < _auto_count_1_3;
    goto _auto_loop_condition_0;
    do
    {
      {
        value_4 = RogueByteList__get__RogueInt32( _auto_collection_0_2, i_1 );
        if (i_1 > 0)
        {
          {
            RogueString__print__RogueCharacter( result_0, ',' );
          }
        }
        RogueString__print__RogueByte( result_0, value_4 );
      }
      {
        ++i_1;
        _auto_condition_0_5 = i_1 < _auto_count_1_3;
      }
      _auto_loop_condition_0:;
    }
    while (_auto_condition_0_5);
    RogueString__print__RogueCharacter( result_0, ']' );
    TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
    return result_0;
  }
}

void RogueByteList__discard_from__RogueInt32( RogueByteList* THISOBJ, RogueInt32 index_0 )
{
  RogueInt32 n_1 = 0;
  (void)n_1;

  {
    n_1 = THISOBJ->count - index_0;
    if (n_1 > 0)
    {
      {
        RogueByteList__zero__RogueInt32_RogueOptionalInt32( THISOBJ, index_0, (RogueOptionalInt32) {n_1,1} );
        THISOBJ->count = index_0;
      }
    }
  }
}

void RogueByteList__ensure_capacity__RogueInt32( RogueByteList* THISOBJ, RogueInt32 desired_capacity_0 )
{
  {
    RogueByteList__reserve__RogueInt32( THISOBJ, desired_capacity_0 - THISOBJ->count );
  }
}

void RogueByteList__expand_to_count__RogueInt32( RogueByteList* THISOBJ, RogueInt32 minimum_count_0 )
{
  {
    if (THISOBJ->count < minimum_count_0)
    {
      {
        RogueByteList__ensure_capacity__RogueInt32( THISOBJ, minimum_count_0 );
        THISOBJ->count = minimum_count_0;
      }
    }
  }
}

RogueByte RogueByteList__first( RogueByteList* THISOBJ )
{
  {
    RogueByte _auto_result_0 = RogueByteList__get__RogueInt32( THISOBJ, 0 );
    return _auto_result_0;
  }
}

RogueByte RogueByteList__get__RogueInt32( RogueByteList* THISOBJ, RogueInt32 index_0 )
{
  {
    {
      {
        RogueByte _auto_result_0 = ((RogueByte*)(THISOBJ->data))[index_0];
        return _auto_result_0;
      }
    }
  }
}

void RogueByteList__on_return_to_pool( RogueByteList* THISOBJ )
{
  {
    RogueByteList__clear(THISOBJ);
  }
}

RogueByte RogueByteList__remove_first( RogueByteList* THISOBJ )
{
  RogueByte result_0 = 0;
  (void)result_0;

  {
    result_0 = RogueByteList__get__RogueInt32( THISOBJ, 0 );
    RogueByteList__shift__RogueInt32( THISOBJ, -1 );
    return result_0;
  }
}

void RogueByteList__reserve__RogueInt32( RogueByteList* THISOBJ, RogueInt32 additional_capacity_0 )
{
  RogueInt32 required_capacity_1 = 0;
  (void)required_capacity_1;

  {
    required_capacity_1 = (THISOBJ->count + additional_capacity_0);
    if (required_capacity_1 <= THISOBJ->capacity)
    {
      {
        return;
      }
    }
    required_capacity_1 = RogueInt32__or_larger__RogueInt32( RogueInt32__or_larger__RogueInt32( required_capacity_1, THISOBJ->count * 2 ), 10 );
    int total_size = required_capacity_1 * THISOBJ->element_size;
    RogueMM_bytes_allocated_since_gc += total_size;
    RogueByte* new_data = (RogueByte*) ROGUE_MALLOC( total_size );

    if (THISOBJ->as_bytes)
    {
      int old_size = THISOBJ->capacity * THISOBJ->element_size;
      RogueByte* old_data = THISOBJ->as_bytes;
      memcpy( new_data, old_data, old_size );
      memset( new_data+old_size, 0, total_size-old_size );
      if (THISOBJ->is_borrowed) THISOBJ->is_borrowed = 0;
      else              ROGUE_FREE( old_data );
    }
    else
    {
      memset( new_data, 0, total_size );
    }
    THISOBJ->as_bytes = new_data;
    THISOBJ->capacity = required_capacity_1;
  }
}

void RogueByteList__set__RogueInt32_RogueByte( RogueByteList* THISOBJ, RogueInt32 index_0, RogueByte value_1 )
{
  {
    {
      {
        ((RogueByte*)(THISOBJ->data))[index_0] = value_1;
      }
    }
  }
}

void RogueByteList__shift__RogueInt32( RogueByteList* THISOBJ, RogueInt32 delta_0 )
{
  {
    if (delta_0 == 0)
    {
      {
        return;
      }
    }
    if (delta_0 > 0)
    {
      {
        RogueByteList__reserve__RogueInt32( THISOBJ, delta_0 );
        RogueByteList__copy__RogueInt32_RogueInt32_RogueByteList_RogueInt32( THISOBJ, 0, THISOBJ->count, THISOBJ, delta_0 );
        RogueByteList__zero__RogueInt32_RogueOptionalInt32( THISOBJ, 0, (RogueOptionalInt32) {delta_0,1} );
        THISOBJ->count += delta_0;
      }
    }
    else
    {
      {
        if ((-delta_0) >= THISOBJ->count)
        {
          {
            RogueByteList__clear(THISOBJ);
          }
        }
        else
        {
          {
            {
              {
                RogueByteList__copy__RogueInt32_RogueInt32_RogueByteList_RogueInt32( THISOBJ, -delta_0, (THISOBJ->count + delta_0), THISOBJ, 0 );
                RogueByteList__discard_from__RogueInt32( THISOBJ, (THISOBJ->count + delta_0) );
              }
            }
          }
        }
      }
    }
  }
}

RogueString* RogueByteList__toxRogueStringx( RogueByteList* THISOBJ )
{
  {
    RogueString* _auto_result_0 = (RogueString*)RogueByteList__description(THISOBJ);
    return _auto_result_0;
  }
}

void RogueByteList__zero__RogueInt32_RogueOptionalInt32( RogueByteList* THISOBJ, RogueInt32 i1_0, RogueOptionalInt32 count_1 )
{
  RogueInt32 n_2 = 0;
  (void)n_2;

  {
    n_2 = (count_1.exists ? count_1.value : THISOBJ->count);
    memset( THISOBJ->as_bytes + i1_0*THISOBJ->element_size, 0, n_2*THISOBJ->element_size );
  }
}

RogueString* RogueByteList__type_name( RogueByteList* THISOBJ )
{
  {
    RogueString* _auto_result_0 = (RogueString*)RogueObject____type_name__RogueInt32( 17 );
    return _auto_result_0;
  }
}

void RogueString_gc_trace( void* THISOBJ )
{
  ((RogueObject*)THISOBJ)->__refcount = ~((RogueObject*)THISOBJ)->__refcount;

  RogueObject* ref;
  if ((ref = (RogueObject*)((RogueString*)THISOBJ)->data) && ref->__refcount >= 0) ref->__type->fn_gc_trace(ref);
}

RogueLogical RogueString__operator____RogueString_RogueString(RogueString* a_0, RogueString* b_1)
{
  {
    if ((void*)a_0 == (void*)0)
    {
      {
        return (void*)b_1 == (void*)0;
      }
    }
    else
    {
      {
        if ((void*)b_1 == (void*)0)
        {
          {
            return 0;
          }
        }
      }
    }
    if ((RogueString__hash_code(a_0) != RogueString__hash_code(b_1)) || (RogueString__count(a_0) != RogueString__count(b_1)))
    {
      {
        return 0;
      }
    }
    RogueLogical _auto_result_0 = (0==memcmp(a_0->data->as_utf8,b_1->data->as_utf8,a_0->data->count));
    return _auto_result_0;
  }
}

RogueString* RogueString__operatorPLUS__RogueString_RogueCharacter(RogueString* left_0, RogueCharacter right_1)
{
  RogueString* _auto_context_block_0_2 = 0;
  (void)_auto_context_block_0_2;
  RogueString* _auto_context_block_1_3 = 0;
  (void)_auto_context_block_1_3;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    if (!!left_0)
    {
      {
        RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_context_block_0_2 );
        _auto_context_block_0_2 = ROGUE_CREATE_OBJECT( RogueString );
        RogueString__init(_auto_context_block_0_2);
        RogueString__reserve__RogueInt32( _auto_context_block_0_2, (RogueString__count(left_0) + 4) );
        RogueString__print__RogueString( _auto_context_block_0_2, left_0 );
        RogueString__print__RogueCharacter( _auto_context_block_0_2, right_1 );
        TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
        return _auto_context_block_0_2;
      }
    }
    else
    {
      {
        {
          {
            RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_context_block_1_3 );
            _auto_context_block_1_3 = ROGUE_CREATE_OBJECT( RogueString );
            RogueString__init(_auto_context_block_1_3);
            RogueString__print__RogueString( _auto_context_block_1_3, str_null );
            RogueString__print__RogueCharacter( _auto_context_block_1_3, right_1 );
            TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
            return _auto_context_block_1_3;
          }
        }
      }
    }
  }
}

RogueString* RogueString__operatorPLUS__RogueString_RogueString(RogueString* left_0, RogueString* right_1)
{
  RogueString* _auto_context_block_0_2 = 0;
  (void)_auto_context_block_0_2;
  RogueString* _auto_context_block_1_3 = 0;
  (void)_auto_context_block_1_3;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    if ((void*)right_1 == (void*)0)
    {
      {
        RogueString* _auto_result_0 = (RogueString*)RogueString__operatorPLUS__RogueString_RogueString( left_0, str_null );
        TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
        return _auto_result_0;
      }
    }
    if (!!left_0)
    {
      {
        RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_context_block_0_2 );
        _auto_context_block_0_2 = ROGUE_CREATE_OBJECT( RogueString );
        RogueString__init(_auto_context_block_0_2);
        RogueString__reserve__RogueInt32( _auto_context_block_0_2, (RogueString__count(left_0) + RogueString__count(right_1)) );
        RogueString__print__RogueString( _auto_context_block_0_2, left_0 );
        RogueString__print__RogueString( _auto_context_block_0_2, right_1 );
        TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
        return _auto_context_block_0_2;
      }
    }
    else
    {
      {
        {
          {
            RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_context_block_1_3 );
            _auto_context_block_1_3 = ROGUE_CREATE_OBJECT( RogueString );
            RogueString__init(_auto_context_block_1_3);
            RogueString__reserve__RogueInt32( _auto_context_block_1_3, (4 + RogueString__count(right_1)) );
            RogueString__print__RogueString( _auto_context_block_1_3, str_null );
            RogueString__print__RogueString( _auto_context_block_1_3, right_1 );
            TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
            return _auto_context_block_1_3;
          }
        }
      }
    }
  }
}

void RogueString__init( RogueString* THISOBJ )
{
  {
    RogueString__init__RogueInt32( THISOBJ, 20 );
  }
}

void RogueString__init__RogueInt32( RogueString* THISOBJ, RogueInt32 byte_capacity_0 )
{
  RogueByteList* _auto_obj_0_1 = 0;
  (void)_auto_obj_0_1;
  RogueByteList* _auto_anchored_context_1_2 = 0;
  (void)_auto_anchored_context_1_2;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    if (THISOBJ->is_immutable)
    {
      {
        TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
        return;
      }
    }
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_obj_0_1 );
    _auto_obj_0_1 = ROGUE_CREATE_OBJECT( RogueByteList );
    RogueByteList__init__RogueInt32( _auto_obj_0_1, (byte_capacity_0 + 1) );
    THISOBJ->data = _auto_obj_0_1;
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_1_2 );
    RogueByteList__set__RogueInt32_RogueByte( (_auto_anchored_context_1_2=THISOBJ->data), 0, ((RogueByte)0) );
    THISOBJ->is_ascii = 1;
  }
  TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
}

void RogueString__clear( RogueString* THISOBJ )
{
  RogueByteList* _auto_anchored_context_0_0 = 0;
  (void)_auto_anchored_context_0_0;
  RogueByteList* _auto_anchored_context_1_1 = 0;
  (void)_auto_anchored_context_1_1;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    if (THISOBJ->is_immutable)
    {
      {
        TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
        return;
      }
    }
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_0_0 );
    RogueByteList__clear((_auto_anchored_context_0_0=THISOBJ->data));
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_1_1 );
    RogueByteList__set__RogueInt32_RogueByte( (_auto_anchored_context_1_1=THISOBJ->data), 0, ((RogueByte)0) );
    THISOBJ->count = 0;
    THISOBJ->indent = 0;
    THISOBJ->at_newline = 1;
    THISOBJ->hash_code = -1;
    THISOBJ->is_ascii = 1;
  }
  TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
}

RogueInt32 RogueString__count( RogueString* THISOBJ )
{
  {
    return THISOBJ->count;
  }
}

void RogueString__flush( RogueString* THISOBJ )
{
}

RogueString* RogueString__from__RogueInt32_RogueInt32( RogueString* THISOBJ, RogueInt32 i1_0, RogueInt32 i2_1 )
{
  RogueInt32 byte_offset_2 = 0;
  (void)byte_offset_2;
  RogueInt32 byte_limit_3 = 0;
  (void)byte_limit_3;
  RogueInt32 byte_count_4 = 0;
  (void)byte_count_4;
  RogueString* result_5 = 0;
  (void)result_5;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;
  (void)_auto_local_pointer_fp_0;

  {
    if (i1_0 < 0)
    {
      {
        i1_0 = 0;
      }
    }
    else
    {
      {
        if (i2_1 >= RogueString__count(THISOBJ))
        {
          {
            i2_1 = RogueString__count(THISOBJ) - 1;
          }
        }
      }
    }
    if (i1_0 > i2_1)
    {
      {
        TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
        return str_;
      }
    }
    if (i1_0 == i2_1)
    {
      {
        RogueString* _auto_result_0 = (RogueString*)RogueString__operatorPLUS__RogueString_RogueCharacter( str_, RogueString__get__RogueInt32( THISOBJ, i1_0 ) );
        TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
        return _auto_result_0;
      }
    }
    if ((i1_0 == 0) && (i2_1 == (RogueString__count(THISOBJ) - 1)))
    {
      {
        RogueString* _auto_result_1 = (RogueString*)THISOBJ;
        TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
        return _auto_result_1;
      }
    }
    byte_offset_2 = RogueString__set_cursor__RogueInt32( THISOBJ, i1_0 );
    byte_limit_3 = RogueString__set_cursor__RogueInt32( THISOBJ, (i2_1 + 1) );
    byte_count_4 = byte_limit_3 - byte_offset_2;
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &result_5 );
    result_5 = ROGUE_CREATE_OBJECT( RogueString );
    RogueString__init__RogueInt32( result_5, byte_count_4 );
    memcpy( result_5->data->as_utf8, THISOBJ->data->as_utf8+byte_offset_2, byte_count_4 );
    result_5->data->count = byte_count_4;
    result_5->count = ((i2_1 - i1_0) + 1);
    result_5->is_ascii = THISOBJ->is_ascii;
    TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
    return result_5;
  }
}

RogueCharacter RogueString__get__RogueInt32( RogueString* THISOBJ, RogueInt32 index_0 )
{
  RogueCharacter result_1 = 0;
  (void)result_1;
  RogueByteList* _auto_anchored_context_0_2 = 0;
  (void)_auto_anchored_context_0_2;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;
  (void)_auto_local_pointer_fp_0;

  {
    RogueString__set_cursor__RogueInt32( THISOBJ, index_0 );
    if (THISOBJ->is_ascii)
    {
      {
        RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_0_2 );
        RogueCharacter _auto_result_0 = ((RogueCharacter)RogueByteList__get__RogueInt32( (_auto_anchored_context_0_2=THISOBJ->data), index_0 ));
        TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
        return _auto_result_0;
      }
    }
    result_1 = 0;

    char* utf8 = THISOBJ->data->as_utf8;

    RogueInt32 offset = THISOBJ->cursor_offset;
    RogueCharacter ch = utf8[ offset ];
    if (ch & 0x80)
    {
      if (ch & 0x20)
      {
        if (ch & 0x10)
        {
          result_1 = ((ch&7)<<18)
              | ((utf8[offset+1] & 0x3F) << 12)
              | ((utf8[offset+2] & 0x3F) << 6)
              | (utf8[offset+3] & 0x3F);
        }
        else
        {
          result_1 = ((ch&15)<<12)
              | ((utf8[offset+1] & 0x3F) << 6)
              | (utf8[offset+2] & 0x3F);
        }
      }
      else
      {
        result_1 = ((ch&31)<<6)
            | (utf8[offset+1] & 0x3F);
      }
    }
    else
    {
      result_1 = ch;
    }
    TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
    return result_1;
  }
}

RogueInt32 RogueString__hash_code( RogueString* THISOBJ )
{
  {
    if (THISOBJ->hash_code == -1)
    {
      {
        THISOBJ->hash_code = RogueString_compute_hash_code( THISOBJ, 0 );
      }
    }
    return THISOBJ->hash_code;
  }
}

RogueString* RogueString__justified__RogueInt32_RogueCharacter( RogueString* THISOBJ, RogueInt32 spaces_0, RogueCharacter fill_1 )
{
  RogueLogical left_2 = 0;
  (void)left_2;
  RogueString* result_3 = 0;
  (void)result_3;
  RogueRangeUpToLessThanIteratorxRogueInt32x _auto_collection_0_4 = {0};
  (void)_auto_collection_0_4;
  RogueOptionalInt32 _auto_next_1_5 = {0};
  (void)_auto_next_1_5;
  RogueLogical _auto_condition_0_6 = 0;
  (void)_auto_condition_0_6;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    left_2 = spaces_0 < 0;
    spaces_0 = RogueInt32__abs(spaces_0);
    if (RogueString__count(THISOBJ) >= spaces_0)
    {
      {
        RogueString* _auto_result_0 = (RogueString*)THISOBJ;
        TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
        return _auto_result_0;
      }
    }
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &result_3 );
    result_3 = ROGUE_CREATE_OBJECT( RogueString );
    RogueString__init__RogueInt32( result_3, spaces_0 );
    if (left_2)
    {
      {
        RogueString__print__RogueString( result_3, THISOBJ );
      }
    }
    _auto_collection_0_4 = (RogueRangeUpToLessThanIteratorxRogueInt32x){0};
    _auto_collection_0_4 = RogueRangeUpToLessThanxRogueInt32x__iterator((RogueRangeUpToLessThanxRogueInt32x) {RogueString__count(THISOBJ),spaces_0,1});
    _auto_next_1_5 = (RogueOptionalInt32){0};
    _auto_next_1_5 = RogueRangeUpToLessThanIteratorxRogueInt32x__read_another(&_auto_collection_0_4);
    _auto_condition_0_6 = _auto_next_1_5.exists;
    goto _auto_loop_condition_1;
    do
    {
      {
        RogueString__print__RogueCharacter( result_3, fill_1 );
      }
      {
        _auto_next_1_5 = RogueRangeUpToLessThanIteratorxRogueInt32x__read_another(&_auto_collection_0_4);
        _auto_condition_0_6 = _auto_next_1_5.exists;
      }
      _auto_loop_condition_1:;
    }
    while (_auto_condition_0_6);
    if (!left_2)
    {
      {
        RogueString__print__RogueString( result_3, THISOBJ );
      }
    }
    TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
    return result_3;
  }
}

RogueString* RogueString__leftmost__RogueInt32( RogueString* THISOBJ, RogueInt32 n_0 )
{
  {
    if (n_0 >= 0)
    {
      {
        RogueString* _auto_result_0 = (RogueString*)RogueString__from__RogueInt32_RogueInt32( THISOBJ, 0, n_0 - 1 );
        return _auto_result_0;
      }
    }
    else
    {
      {
        {
          {
            RogueString* _auto_result_1 = (RogueString*)RogueString__from__RogueInt32_RogueInt32( THISOBJ, 0, ((RogueString__count(THISOBJ) + n_0)) - 1 );
            return _auto_result_1;
          }
        }
      }
    }
  }
}

void RogueString__on_return_to_pool( RogueString* THISOBJ )
{
  {
    RogueString__clear(THISOBJ);
  }
}

void RogueString__print__RogueByte( RogueString* THISOBJ, RogueByte value_0 )
{
  {
    RogueString__print__RogueInt64( THISOBJ, ((RogueInt64)value_0) );
  }
}

void RogueString__print__RogueCharacter( RogueString* THISOBJ, RogueCharacter value_0 )
{
  RogueInt32 _auto_i_0_1 = 0;
  (void)_auto_i_0_1;
  RogueByteList* _auto_context_block_2_2 = 0;
  (void)_auto_context_block_2_2;
  RogueByteList* _auto_context_block_3_3 = 0;
  (void)_auto_context_block_3_3;
  RogueByteList* _auto_context_block_4_4 = 0;
  (void)_auto_context_block_4_4;
  RogueByteList* _auto_context_block_5_5 = 0;
  (void)_auto_context_block_5_5;
  RogueLogical _auto_condition_0_6 = 0;
  (void)_auto_condition_0_6;
  RogueByteList* _auto_anchored_context_1_7 = 0;
  (void)_auto_anchored_context_1_7;
  RogueByteList* _auto_anchored_context_2_8 = 0;
  (void)_auto_anchored_context_2_8;
  RogueByteList* _auto_anchored_context_3_9 = 0;
  (void)_auto_anchored_context_3_9;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    if (THISOBJ->is_immutable)
    {
      {
        TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
        return;
      }
    }
    THISOBJ->hash_code = -1;
    if (value_0 == ((RogueCharacter)10))
    {
      {
        THISOBJ->at_newline = 1;
      }
    }
    else
    {
      {
        if (THISOBJ->at_newline)
        {
          {
            if (!!THISOBJ->indent)
            {
              {
                _auto_i_0_1 = (THISOBJ->indent + 1);
                if (_auto_i_0_1 > 0)
                {
                  {
                    _auto_condition_0_6 = --_auto_i_0_1;
                    goto _auto_loop_condition_0;
                    do
                    {
                      {
                        RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_1_7 );
                        RogueByteList__add__RogueByte( (_auto_anchored_context_1_7=THISOBJ->data), ((RogueByte)' ') );
                      }
                      {
                        _auto_condition_0_6 = --_auto_i_0_1;
                      }
                      _auto_loop_condition_0:;
                    }
                    while (_auto_condition_0_6);
                  }
                }
                THISOBJ->count = (RogueString__count(THISOBJ) + THISOBJ->indent);
              }
            }
            THISOBJ->at_newline = 0;
          }
        }
      }
    }
    THISOBJ->count = (RogueString__count(THISOBJ) + 1);
    if (((RogueInt32)value_0) <= ((RogueInt32)((RogueCharacter)127)))
    {
      {
        RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_2_8 );
        RogueByteList__add__RogueByte( (_auto_anchored_context_2_8=THISOBJ->data), ((RogueByte)value_0) );
      }
    }
    else
    {
      {
        {
          {
            THISOBJ->is_ascii = 0;
            if (((RogueInt32)value_0) <= ((RogueInt32)((RogueCharacter)2047)))
            {
              {
                RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_context_block_2_2 );
                _auto_context_block_2_2 = THISOBJ->data;
                RogueByteList__add__RogueByte( _auto_context_block_2_2, ((RogueByte)(192 | (((RogueInt32)value_0) >> 6))) );
                RogueByteList__add__RogueByte( _auto_context_block_2_2, ((RogueByte)(128 | (((RogueInt32)value_0) & 63))) );
              }
            }
            else
            {
              {
                if (((RogueInt32)value_0) <= ((RogueInt32)((RogueCharacter)65535)))
                {
                  {
                    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_context_block_3_3 );
                    _auto_context_block_3_3 = THISOBJ->data;
                    RogueByteList__add__RogueByte( _auto_context_block_3_3, ((RogueByte)(224 | (((RogueInt32)value_0) >> 12))) );
                    RogueByteList__add__RogueByte( _auto_context_block_3_3, ((RogueByte)(128 | ((((RogueInt32)value_0) >> 6) & 63))) );
                    RogueByteList__add__RogueByte( _auto_context_block_3_3, ((RogueByte)(128 | (((RogueInt32)value_0) & 63))) );
                  }
                }
                else
                {
                  {
                    if (((RogueInt32)value_0) <= ((RogueInt32)((RogueCharacter)1114111)))
                    {
                      {
                        RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_context_block_4_4 );
                        _auto_context_block_4_4 = THISOBJ->data;
                        RogueByteList__add__RogueByte( _auto_context_block_4_4, ((RogueByte)(240 | (((RogueInt32)value_0) >> 18))) );
                        RogueByteList__add__RogueByte( _auto_context_block_4_4, ((RogueByte)(128 | ((((RogueInt32)value_0) >> 12) & 63))) );
                        RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_context_block_5_5 );
                        _auto_context_block_5_5 = THISOBJ->data;
                        RogueByteList__add__RogueByte( _auto_context_block_5_5, ((RogueByte)(128 | ((((RogueInt32)value_0) >> 6) & 63))) );
                        RogueByteList__add__RogueByte( _auto_context_block_5_5, ((RogueByte)(128 | (((RogueInt32)value_0) & 63))) );
                      }
                    }
                    else
                    {
                      {
                        {
                          {
                            RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_3_9 );
                            RogueByteList__add__RogueByte( (_auto_anchored_context_3_9=THISOBJ->data), ((RogueByte)'?') );
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
}

void RogueString__print__RogueInt32( RogueString* THISOBJ, RogueInt32 value_0 )
{
  {
    RogueString__print__RogueInt64( THISOBJ, ((RogueInt64)value_0) );
  }
}

void RogueString__print__RogueInt64( RogueString* THISOBJ, RogueInt64 value_0 )
{
  {
    if (value_0 == (((RogueInt64)1) << ((RogueInt64)63)))
    {
      {
        RogueString__print__RogueString( THISOBJ, str__9223372036854775808 );
      }
    }
    else
    {
      {
        if (value_0 < ((RogueInt64)0))
        {
          {
            RogueString__print__RogueCharacter( THISOBJ, '-' );
            RogueString__print__RogueInt64( THISOBJ, -value_0 );
          }
        }
        else
        {
          {
            if (value_0 >= ((RogueInt64)10))
            {
              {
                RogueString__print__RogueInt64( THISOBJ, (value_0 / ((RogueInt64)10)) );
                RogueString__print__RogueCharacter( THISOBJ, ((RogueCharacter)((((RogueInt64)'0') + RogueInt64__operatorMOD__RogueInt64( value_0, ((RogueInt64)10) )))) );
              }
            }
            else
            {
              {
                {
                  {
                    RogueString__print__RogueCharacter( THISOBJ, ((RogueCharacter)((((RogueInt64)'0') + value_0))) );
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}

void RogueString__print__RogueObject( RogueString* THISOBJ, RogueObject* value_0 )
{
  RogueObject* _auto_virtual_context_0;
  (void) _auto_virtual_context_0;
  RogueString* _auto_anchored_arg_0_0_1 = 0;
  (void)_auto_anchored_arg_0_0_1;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    if (THISOBJ->is_immutable)
    {
      {
        TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
        return;
      }
    }
    if ((void*)value_0 == (void*)0)
    {
      {
        RogueString__print__RogueString( THISOBJ, str_null );
        TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
        return;
      }
    }
    _auto_anchored_arg_0_0_1 = 0;
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_arg_0_0_1 );
    RogueString__print__RogueString( THISOBJ, (_auto_anchored_arg_0_0_1=((void)(_auto_virtual_context_0=(RogueObject*)(value_0)),((RogueString* (*)(RogueObject*))(_auto_virtual_context_0->__type->vtable[0]))((RogueObject*)_auto_virtual_context_0))) );
  }
  TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
}

void RogueString__print__RogueString( RogueString* THISOBJ, RogueString* value_0 )
{
  RogueInt32 _auto_index_0_1 = 0;
  (void)_auto_index_0_1;
  RogueInt32 _auto_count_1_2 = 0;
  (void)_auto_count_1_2;
  RogueCharacter _auto_iterator_0_3 = 0;
  (void)_auto_iterator_0_3;
  RogueLogical _auto_condition_0_4 = 0;
  (void)_auto_condition_0_4;

  {
    if (THISOBJ->is_immutable)
    {
      {
        return;
      }
    }
    if ((void*)value_0 == (void*)0)
    {
      {
        RogueString__print__RogueString( THISOBJ, str_null );
        return;
      }
    }
    if (RogueString__count(value_0) == 0)
    {
      {
        return;
      }
    }
    RogueString__reserve__RogueInt32( THISOBJ, value_0->data->count );
    if (!!THISOBJ->indent)
    {
      {
        _auto_index_0_1 = 0;
        _auto_count_1_2 = 0;
        _auto_count_1_2 = RogueString__count(value_0);
        _auto_iterator_0_3 = 0;
        _auto_condition_0_4 = _auto_index_0_1 < _auto_count_1_2;
        goto _auto_loop_condition_0;
        do
        {
          {
            _auto_iterator_0_3 = RogueString__get__RogueInt32( value_0, _auto_index_0_1 );
            RogueString__print__RogueCharacter( THISOBJ, _auto_iterator_0_3 );
          }
          {
            ++_auto_index_0_1;
            _auto_condition_0_4 = _auto_index_0_1 < _auto_count_1_2;
          }
          _auto_loop_condition_0:;
        }
        while (_auto_condition_0_4);
      }
    }
    else
    {
      {
        {
          {
            memcpy( THISOBJ->data->as_utf8+THISOBJ->data->count, value_0->data->as_utf8, value_0->data->count );
            THISOBJ->count       += value_0->count;
            THISOBJ->data->count += value_0->data->count;
            THISOBJ->data->as_utf8[THISOBJ->data->count] = 0;
            if (!value_0->is_ascii)
            {
              {
                THISOBJ->is_ascii = 0;
              }
            }
          }
        }
      }
    }
    THISOBJ->hash_code = -1;
  }
}

void RogueString__println__RogueObject( RogueString* THISOBJ, RogueObject* value_0 )
{
  {
    RogueString__print__RogueObject( THISOBJ, value_0 );
    RogueString__print__RogueCharacter( THISOBJ, ((RogueCharacter)10) );
  }
}

void RogueString__println__RogueString( RogueString* THISOBJ, RogueString* value_0 )
{
  {
    RogueString__print__RogueString( THISOBJ, value_0 );
    RogueString__print__RogueCharacter( THISOBJ, ((RogueCharacter)10) );
  }
}

void RogueString__reserve__RogueInt32( RogueString* THISOBJ, RogueInt32 additional_bytes_0 )
{
  RogueByteList* _auto_anchored_context_0_1 = 0;
  (void)_auto_anchored_context_0_1;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    if (THISOBJ->is_immutable)
    {
      {
        TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
        return;
      }
    }
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_0_1 );
    RogueByteList__reserve__RogueInt32( (_auto_anchored_context_0_1=THISOBJ->data), (additional_bytes_0 + 1) );
  }
  TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
}

RogueInt32 RogueString__set_cursor__RogueInt32( RogueString* THISOBJ, RogueInt32 character_index_0 )
{
  {
    if (THISOBJ->is_ascii)
    {
      {
        THISOBJ->cursor_offset = character_index_0;
        THISOBJ->cursor_index = character_index_0;
        return THISOBJ->cursor_offset;
      }
    }
    char* utf8 = THISOBJ->data->as_utf8;

    RogueInt32 c_offset;
    RogueInt32 c_index;

    if (character_index_0 == 0)
    {
      THISOBJ->cursor_index = 0;
      THISOBJ->cursor_offset = 0;
    return 0;
    }
    else if (character_index_0 >= THISOBJ->count - 1 || THISOBJ->cursor_index > THISOBJ->count-1 || THISOBJ->cursor_offset > THISOBJ->data->count-1)
    {
      c_offset = THISOBJ->data->count;
      c_index = THISOBJ->count;
    }
    else
    {
      c_offset = THISOBJ->cursor_offset;
      c_index  = THISOBJ->cursor_index;
    }

    while (c_index < character_index_0)
    {
      while ((utf8[++c_offset] & 0xC0) == 0x80) {}
      ++c_index;
    }

    while (c_index > character_index_0)
    {
      while ((utf8[--c_offset] & 0xC0) == 0x80) {}
      --c_index;
    }

    THISOBJ->cursor_index = c_index;
    THISOBJ->cursor_offset = c_offset;
    return THISOBJ->cursor_offset;
  }
}

RogueString* RogueString__toxRogueStringx( RogueString* THISOBJ )
{
  {
    RogueString* _auto_result_0 = (RogueString*)THISOBJ;
    return _auto_result_0;
  }
}

void RogueString__init_object( RogueString* THISOBJ )
{
  {
    RogueObject__init_object(((RogueObject*)THISOBJ));
    THISOBJ->hash_code = -1;
    THISOBJ->at_newline = 1;
  }
}

RogueString* RogueString__type_name( RogueString* THISOBJ )
{
  {
    RogueString* _auto_result_0 = (RogueString*)RogueObject____type_name__RogueInt32( 18 );
    return _auto_result_0;
  }
}

void RogueOPARENFunctionOPARENCPARENCPAREN_gc_trace( void* THISOBJ )
{
  ((RogueObject*)THISOBJ)->__refcount = ~((RogueObject*)THISOBJ)->__refcount;
}

void RogueOPARENFunctionOPARENCPARENCPAREN__init_object( RogueOPARENFunctionOPARENCPARENCPAREN* THISOBJ )
{
  {
    RogueObject__init_object(((RogueObject*)THISOBJ));
  }
}

RogueString* RogueOPARENFunctionOPARENCPARENCPAREN__type_name( RogueOPARENFunctionOPARENCPARENCPAREN* THISOBJ )
{
  {
    RogueString* _auto_result_0 = (RogueString*)RogueObject____type_name__RogueInt32( 20 );
    return _auto_result_0;
  }
}

void RogueOPARENFunctionOPARENCPARENCPARENList_gc_trace( void* THISOBJ )
{
  ((RogueObject*)THISOBJ)->__refcount = ~((RogueObject*)THISOBJ)->__refcount;

  RogueObject* ref;
  if ((ref = (RogueObject*)((RogueOPARENFunctionOPARENCPARENCPARENList*)THISOBJ)->element_type) && ref->__refcount >= 0) ref->__type->fn_gc_trace(ref);

  RogueObject** cursor = ((RogueObject**)((RogueOPARENFunctionOPARENCPARENCPARENList*)THISOBJ)->data) - 1;
  int n = ((int)((RogueOPARENFunctionOPARENCPARENCPARENList*)THISOBJ)->capacity);
  while (--n >=0)
  {
    if ((ref = *(++cursor)) && ref->__refcount >= 0) ref->__type->fn_gc_trace(ref);
  }
}

void RogueOPARENFunctionOPARENCPARENCPARENList__init( RogueOPARENFunctionOPARENCPARENCPARENList* THISOBJ )
{
}

void RogueOPARENFunctionOPARENCPARENCPARENList__init_object( RogueOPARENFunctionOPARENCPARENCPARENList* THISOBJ )
{
  {
    RogueObject__init_object(((RogueObject*)THISOBJ));
    {
      {
        THISOBJ->element_size = sizeof(THISOBJ->element_type);
      }
    }
    {
      THISOBJ->is_ref_array = 1;
    }
  }
}

void RogueOPARENFunctionOPARENCPARENCPARENList__on_cleanup( RogueOPARENFunctionOPARENCPARENCPARENList* THISOBJ )
{
  {
    if (THISOBJ->as_bytes)
    {
      if (THISOBJ->is_borrowed) THISOBJ->is_borrowed = 0;
      else              ROGUE_FREE( THISOBJ->as_bytes );
      THISOBJ->as_bytes = 0;
      THISOBJ->capacity = 0;
      THISOBJ->count = 0;
    }
  }
}

void RogueOPARENFunctionOPARENCPARENCPARENList__add__RogueOPARENFunctionOPARENCPARENCPAREN( RogueOPARENFunctionOPARENCPARENCPARENList* THISOBJ, RogueOPARENFunctionOPARENCPARENCPAREN* value_0 )
{
  {
    RogueOPARENFunctionOPARENCPARENCPARENList__reserve__RogueInt32( THISOBJ, 1 );
    {
      {
        ((RogueOPARENFunctionOPARENCPARENCPAREN**)(THISOBJ->data))[THISOBJ->count++] = value_0;
      }
    }
  }
}

void RogueOPARENFunctionOPARENCPARENCPARENList__clear( RogueOPARENFunctionOPARENCPARENCPARENList* THISOBJ )
{
  {
    RogueOPARENFunctionOPARENCPARENCPARENList__discard_from__RogueInt32( THISOBJ, 0 );
  }
}

RogueString* RogueOPARENFunctionOPARENCPARENCPARENList__description( RogueOPARENFunctionOPARENCPARENCPARENList* THISOBJ )
{
  RogueString* result_0 = 0;
  (void)result_0;
  RogueInt32 i_1 = 0;
  (void)i_1;
  RogueOPARENFunctionOPARENCPARENCPARENList* _auto_collection_0_2 = 0;
  (void)_auto_collection_0_2;
  RogueInt32 _auto_count_1_3 = 0;
  (void)_auto_count_1_3;
  RogueOPARENFunctionOPARENCPARENCPAREN* value_4 = 0;
  (void)value_4;
  RogueLogical _auto_condition_0_5 = 0;
  (void)_auto_condition_0_5;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &result_0 );
    result_0 = ROGUE_CREATE_OBJECT( RogueString );
    RogueString__init(result_0);
    RogueString__print__RogueCharacter( result_0, '[' );
    i_1 = 0;
    _auto_collection_0_2 = 0;
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_collection_0_2 );
    _auto_collection_0_2 = THISOBJ;
    _auto_count_1_3 = 0;
    _auto_count_1_3 = _auto_collection_0_2->count;
    value_4 = 0;
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &value_4 );
    _auto_condition_0_5 = i_1 < _auto_count_1_3;
    goto _auto_loop_condition_0;
    do
    {
      {
        value_4 = RogueOPARENFunctionOPARENCPARENCPARENList__get__RogueInt32( _auto_collection_0_2, i_1 );
        if (i_1 > 0)
        {
          {
            RogueString__print__RogueCharacter( result_0, ',' );
          }
        }
        RogueString__print__RogueObject( result_0, ((RogueObject*)value_4) );
      }
      {
        ++i_1;
        _auto_condition_0_5 = i_1 < _auto_count_1_3;
      }
      _auto_loop_condition_0:;
    }
    while (_auto_condition_0_5);
    RogueString__print__RogueCharacter( result_0, ']' );
    TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
    return result_0;
  }
}

void RogueOPARENFunctionOPARENCPARENCPARENList__discard_from__RogueInt32( RogueOPARENFunctionOPARENCPARENCPARENList* THISOBJ, RogueInt32 index_0 )
{
  RogueInt32 n_1 = 0;
  (void)n_1;

  {
    n_1 = THISOBJ->count - index_0;
    if (n_1 > 0)
    {
      {
        RogueOPARENFunctionOPARENCPARENCPARENList__zero__RogueInt32_RogueOptionalInt32( THISOBJ, index_0, (RogueOptionalInt32) {n_1,1} );
        THISOBJ->count = index_0;
      }
    }
  }
}

RogueOPARENFunctionOPARENCPARENCPAREN* RogueOPARENFunctionOPARENCPARENCPARENList__get__RogueInt32( RogueOPARENFunctionOPARENCPARENCPARENList* THISOBJ, RogueInt32 index_0 )
{
  {
    {
      {
        RogueOPARENFunctionOPARENCPARENCPAREN* _auto_result_0 = (RogueOPARENFunctionOPARENCPARENCPAREN*)((RogueOPARENFunctionOPARENCPARENCPAREN**)(THISOBJ->data))[index_0];
        return _auto_result_0;
      }
    }
  }
}

void RogueOPARENFunctionOPARENCPARENCPARENList__on_return_to_pool( RogueOPARENFunctionOPARENCPARENCPARENList* THISOBJ )
{
  {
    RogueOPARENFunctionOPARENCPARENCPARENList__clear(THISOBJ);
  }
}

void RogueOPARENFunctionOPARENCPARENCPARENList__reserve__RogueInt32( RogueOPARENFunctionOPARENCPARENCPARENList* THISOBJ, RogueInt32 additional_capacity_0 )
{
  RogueInt32 required_capacity_1 = 0;
  (void)required_capacity_1;

  {
    required_capacity_1 = (THISOBJ->count + additional_capacity_0);
    if (required_capacity_1 <= THISOBJ->capacity)
    {
      {
        return;
      }
    }
    required_capacity_1 = RogueInt32__or_larger__RogueInt32( RogueInt32__or_larger__RogueInt32( required_capacity_1, THISOBJ->count * 2 ), 10 );
    int total_size = required_capacity_1 * THISOBJ->element_size;
    RogueMM_bytes_allocated_since_gc += total_size;
    RogueByte* new_data = (RogueByte*) ROGUE_MALLOC( total_size );

    if (THISOBJ->as_bytes)
    {
      int old_size = THISOBJ->capacity * THISOBJ->element_size;
      RogueByte* old_data = THISOBJ->as_bytes;
      memcpy( new_data, old_data, old_size );
      memset( new_data+old_size, 0, total_size-old_size );
      if (THISOBJ->is_borrowed) THISOBJ->is_borrowed = 0;
      else              ROGUE_FREE( old_data );
    }
    else
    {
      memset( new_data, 0, total_size );
    }
    THISOBJ->as_bytes = new_data;
    THISOBJ->capacity = required_capacity_1;
  }
}

RogueString* RogueOPARENFunctionOPARENCPARENCPARENList__toxRogueStringx( RogueOPARENFunctionOPARENCPARENCPARENList* THISOBJ )
{
  {
    RogueString* _auto_result_0 = (RogueString*)RogueOPARENFunctionOPARENCPARENCPARENList__description(THISOBJ);
    return _auto_result_0;
  }
}

void RogueOPARENFunctionOPARENCPARENCPARENList__zero__RogueInt32_RogueOptionalInt32( RogueOPARENFunctionOPARENCPARENCPARENList* THISOBJ, RogueInt32 i1_0, RogueOptionalInt32 count_1 )
{
  RogueInt32 n_2 = 0;
  (void)n_2;

  {
    n_2 = (count_1.exists ? count_1.value : THISOBJ->count);
    memset( THISOBJ->as_bytes + i1_0*THISOBJ->element_size, 0, n_2*THISOBJ->element_size );
  }
}

RogueString* RogueOPARENFunctionOPARENCPARENCPARENList__type_name( RogueOPARENFunctionOPARENCPARENCPARENList* THISOBJ )
{
  {
    RogueString* _auto_result_0 = (RogueString*)RogueObject____type_name__RogueInt32( 21 );
    return _auto_result_0;
  }
}

void RogueGlobal_gc_trace( void* THISOBJ )
{
  ((RogueObject*)THISOBJ)->__refcount = ~((RogueObject*)THISOBJ)->__refcount;

  RogueObject* ref;
  if ((ref = (RogueObject*)((RogueGlobal*)THISOBJ)->global_output_buffer) && ref->__refcount >= 0) ref->__type->fn_gc_trace(ref);
  if ((ref = (RogueObject*)((RogueGlobal*)THISOBJ)->output) && ref->__refcount >= 0) ref->__type->fn_gc_trace(ref);
  if ((ref = (RogueObject*)((RogueGlobal*)THISOBJ)->error) && ref->__refcount >= 0) ref->__type->fn_gc_trace(ref);
  if ((ref = (RogueObject*)((RogueGlobal*)THISOBJ)->log) && ref->__refcount >= 0) ref->__type->fn_gc_trace(ref);
  if ((ref = (RogueObject*)((RogueGlobal*)THISOBJ)->exit_functions) && ref->__refcount >= 0) ref->__type->fn_gc_trace(ref);
}

void RogueGlobal__call_exit_functions(void)
{
  RogueObject* _auto_virtual_context_0;
  (void) _auto_virtual_context_0;
  RogueOPARENFunctionOPARENCPARENCPARENList* functions_0 = 0;
  (void)functions_0;
  RogueInt32 _auto_index_0_1 = 0;
  (void)_auto_index_0_1;
  RogueInt32 _auto_count_1_2 = 0;
  (void)_auto_count_1_2;
  RogueOPARENFunctionOPARENCPARENCPAREN* fn_3 = 0;
  (void)fn_3;
  RogueLogical _auto_condition_0_4 = 0;
  (void)_auto_condition_0_4;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &functions_0 );
    functions_0 = ROGUE_SINGLETON(RogueGlobal)->exit_functions;
    ROGUE_SINGLETON(RogueGlobal)->exit_functions = 0;
    if (!!functions_0)
    {
      {
        _auto_index_0_1 = 0;
        _auto_count_1_2 = 0;
        _auto_count_1_2 = functions_0->count;
        fn_3 = 0;
        RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &fn_3 );
        _auto_condition_0_4 = _auto_index_0_1 < _auto_count_1_2;
        goto _auto_loop_condition_0;
        do
        {
          {
            fn_3 = RogueOPARENFunctionOPARENCPARENCPARENList__get__RogueInt32( functions_0, _auto_index_0_1 );
            ((void)(_auto_virtual_context_0=(RogueObject*)(fn_3)),((void (*)(RogueOPARENFunctionOPARENCPARENCPAREN*))(_auto_virtual_context_0->__type->vtable[3]))((RogueOPARENFunctionOPARENCPARENCPAREN*)_auto_virtual_context_0));
          }
          {
            ++_auto_index_0_1;
            _auto_condition_0_4 = _auto_index_0_1 < _auto_count_1_2;
          }
          _auto_loop_condition_0:;
        }
        while (_auto_condition_0_4);
      }
    }
  }
  TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
}

void RogueGlobal__on_control_c__RogueInt32(RogueInt32 signum_0)
{
  {
    RogueGlobal__call_exit_functions();
    RogueSystem__exit__RogueInt32( 1 );
  }
}

void RogueGlobal__on_segmentation_fault__RogueInt32(RogueInt32 signum_0)
{
  RogueObject* _auto_anchored_context_0_1 = 0;
  (void)_auto_anchored_context_0_1;
  RogueObject* _auto_anchored_context_1_2 = 0;
  (void)_auto_anchored_context_1_2;
  RogueStackTrace* _auto_obj_2_3 = 0;
  (void)_auto_obj_2_3;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_0_1 );
    Rogue_dispatch_println__RogueString( (_auto_anchored_context_0_1=RogueConsole__error(ROGUE_SINGLETON(RogueConsole))), str____SEGFAULT___ );
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_1_2 );
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_obj_2_3 );
    _auto_obj_2_3 = ROGUE_CREATE_OBJECT( RogueStackTrace );
    RogueStackTrace__init(_auto_obj_2_3);
    Rogue_dispatch_println__RogueObject( (_auto_anchored_context_1_2=RogueConsole__error(ROGUE_SINGLETON(RogueConsole))), ((RogueObject*)_auto_obj_2_3) );
    RogueGlobal__call_exit_functions();
    RogueSystem__exit__RogueInt32( 1 );
  }
  TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
}

void RogueGlobal__init( RogueGlobal* THISOBJ )
{
  {
    #if !defined(__EMSCRIPTEN__)
    signal( SIGINT,  (void(*)(int))RogueGlobal__on_control_c__RogueInt32 );
    signal( SIGSEGV, (void(*)(int))RogueGlobal__on_segmentation_fault__RogueInt32 );
    #endif // !defined(__EMSCRIPTEN__)
    RogueGlobal__configure_standard_output(THISOBJ);
    RogueGlobal__on_exit__RogueOPARENFunctionOPARENCPARENCPAREN( THISOBJ, ((RogueOPARENFunctionOPARENCPARENCPAREN*)ROGUE_SINGLETON(RogueFunction_262)) );
  }
}

void RogueGlobal__configure_standard_output( RogueGlobal* THISOBJ )
{
  {
    THISOBJ->output = ((RogueObject*)ROGUE_SINGLETON(RogueConsole));
    THISOBJ->error = RogueConsole__error(ROGUE_SINGLETON(RogueConsole));
    THISOBJ->log = ((RogueObject*)ROGUE_SINGLETON(RogueConsole));
  }
}

void RogueGlobal__flush__RogueString( RogueGlobal* THISOBJ, RogueString* buffer_0 )
{
  RogueObject* _auto_context_block_0_1 = 0;
  (void)_auto_context_block_0_1;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_context_block_0_1 );
    _auto_context_block_0_1 = THISOBJ->output;
    Rogue_dispatch_print__RogueString( _auto_context_block_0_1, buffer_0 );
    Rogue_dispatch_flush_(_auto_context_block_0_1);
    RogueString__clear(buffer_0);
  }
  TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
}

void RogueGlobal__on_exit__RogueOPARENFunctionOPARENCPARENCPAREN( RogueGlobal* THISOBJ, RogueOPARENFunctionOPARENCPARENCPAREN* fn_0 )
{
  RogueOPARENFunctionOPARENCPARENCPARENList* _auto_obj_0_1 = 0;
  (void)_auto_obj_0_1;
  RogueOPARENFunctionOPARENCPARENCPARENList* _auto_anchored_context_1_2 = 0;
  (void)_auto_anchored_context_1_2;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    if (!THISOBJ->exit_functions)
    {
      {
        RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_obj_0_1 );
        _auto_obj_0_1 = ROGUE_CREATE_OBJECT( RogueOPARENFunctionOPARENCPARENCPARENList );
        RogueOPARENFunctionOPARENCPARENCPARENList__init(_auto_obj_0_1);
        THISOBJ->exit_functions = _auto_obj_0_1;
      }
    }
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_1_2 );
    RogueOPARENFunctionOPARENCPARENCPARENList__add__RogueOPARENFunctionOPARENCPARENCPAREN( (_auto_anchored_context_1_2=THISOBJ->exit_functions), fn_0 );
  }
  TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
}

void RogueGlobal__init_object( RogueGlobal* THISOBJ )
{
  RogueString* _auto_obj_0_0 = 0;
  (void)_auto_obj_0_0;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    RogueObject__init_object(((RogueObject*)THISOBJ));
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_obj_0_0 );
    _auto_obj_0_0 = ROGUE_CREATE_OBJECT( RogueString );
    RogueString__init(_auto_obj_0_0);
    THISOBJ->global_output_buffer = _auto_obj_0_0;
  }
  TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
}

RogueString* RogueGlobal__type_name( RogueGlobal* THISOBJ )
{
  {
    RogueString* _auto_result_0 = (RogueString*)RogueObject____type_name__RogueInt32( 22 );
    return _auto_result_0;
  }
}

void RogueGlobal__flush( RogueGlobal* THISOBJ )
{
  RogueString* _auto_anchored_arg_0_0_0 = 0;
  (void)_auto_anchored_arg_0_0_0;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    _auto_anchored_arg_0_0_0 = 0;
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_arg_0_0_0 );
    RogueGlobal__flush__RogueString( THISOBJ, (_auto_anchored_arg_0_0_0=THISOBJ->global_output_buffer) );
  }
  TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
}

void RogueGlobal__print__RogueCharacter( RogueGlobal* THISOBJ, RogueCharacter value_0 )
{
  RogueString* _auto_anchored_context_0_1 = 0;
  (void)_auto_anchored_context_0_1;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_0_1 );
    RogueString__print__RogueCharacter( (_auto_anchored_context_0_1=THISOBJ->global_output_buffer), value_0 );
    if (value_0 == ((RogueCharacter)10))
    {
      {
        RogueGlobal__flush(THISOBJ);
      }
    }
  }
  TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
}

void RogueGlobal__print__RogueObject( RogueGlobal* THISOBJ, RogueObject* value_0 )
{
  RogueString* _auto_anchored_context_0_1 = 0;
  (void)_auto_anchored_context_0_1;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_0_1 );
    RogueString__print__RogueObject( (_auto_anchored_context_0_1=THISOBJ->global_output_buffer), value_0 );
  }
  TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
}

void RogueGlobal__print__RogueString( RogueGlobal* THISOBJ, RogueString* value_0 )
{
  RogueString* _auto_anchored_context_0_1 = 0;
  (void)_auto_anchored_context_0_1;
  RogueString* _auto_anchored_context_1_2 = 0;
  (void)_auto_anchored_context_1_2;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_0_1 );
    RogueString__print__RogueString( (_auto_anchored_context_0_1=THISOBJ->global_output_buffer), value_0 );
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_1_2 );
    if (RogueString__count((_auto_anchored_context_1_2=THISOBJ->global_output_buffer)) > 1024)
    {
      {
        RogueGlobal__flush(THISOBJ);
      }
    }
  }
  TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
}

void RogueGlobal__println( RogueGlobal* THISOBJ )
{
  RogueString* _auto_anchored_context_0_0 = 0;
  (void)_auto_anchored_context_0_0;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_0_0 );
    RogueString__print__RogueCharacter( (_auto_anchored_context_0_0=THISOBJ->global_output_buffer), ((RogueCharacter)10) );
    RogueGlobal__flush(THISOBJ);
  }
  TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
}

void RogueGlobal__println__RogueObject( RogueGlobal* THISOBJ, RogueObject* value_0 )
{
  {
    RogueGlobal__print__RogueObject( THISOBJ, value_0 );
    RogueGlobal__println(THISOBJ);
  }
}

void RogueGlobal__println__RogueString( RogueGlobal* THISOBJ, RogueString* value_0 )
{
  {
    RogueGlobal__print__RogueString( THISOBJ, value_0 );
    RogueGlobal__println(THISOBJ);
  }
}

void RogueObject_gc_trace( void* THISOBJ )
{
  ((RogueObject*)THISOBJ)->__refcount = ~((RogueObject*)THISOBJ)->__refcount;
}

RogueString* RogueObject____type_name__RogueInt32(RogueInt32 type_index_0)
{
  {
    if (!Rogue_types[type_index_0]->name_object)
    {
      {
        Rogue_types[type_index_0]->name_object = RogueString_create_permanent( Rogue_types[type_index_0]->name );
      }
    }
    RogueString* _auto_result_0 = (RogueString*)Rogue_types[type_index_0]->name_object;
    return _auto_result_0;
  }
}

RogueString* RogueObject__toxRogueStringx( RogueObject* THISOBJ )
{
  {
    RogueString* _auto_result_0 = (RogueString*)Rogue_dispatch_type_name___RogueObject(THISOBJ);
    return _auto_result_0;
  }
}

void RogueObject__init_object( RogueObject* THISOBJ )
{
}

RogueString* RogueObject__type_name( RogueObject* THISOBJ )
{
  {
    RogueString* _auto_result_0 = (RogueString*)RogueObject____type_name__RogueInt32( 24 );
    return _auto_result_0;
  }
}

void RogueStackTraceFrameList_gc_trace( void* THISOBJ )
{
  ((RogueObject*)THISOBJ)->__refcount = ~((RogueObject*)THISOBJ)->__refcount;

  RogueObject* ref;
  if ((ref = (RogueObject*)(((RogueStackTraceFrameList*)THISOBJ)->element_type.procedure)) && ref->__refcount >= 0) ref->__type->fn_gc_trace(ref);
  if ((ref = (RogueObject*)(((RogueStackTraceFrameList*)THISOBJ)->element_type.filename)) && ref->__refcount >= 0) ref->__type->fn_gc_trace(ref);

  RogueStackTraceFrame* cursor = (RogueStackTraceFrame*)(((RogueStackTraceFrameList*)THISOBJ)->data);
  int n = ((int)((RogueStackTraceFrameList*)THISOBJ)->capacity);
  while (--n >=0)
  {
    if ((ref = (RogueObject*)((*cursor).procedure)) && ref->__refcount >= 0) ref->__type->fn_gc_trace(ref);
    if ((ref = (RogueObject*)((*cursor).filename)) && ref->__refcount >= 0) ref->__type->fn_gc_trace(ref);
    ++cursor;
  }
}

void RogueStackTraceFrameList__init__RogueInt32( RogueStackTraceFrameList* THISOBJ, RogueInt32 capacity_0 )
{
  {
    RogueStackTraceFrameList__reserve__RogueInt32( THISOBJ, capacity_0 );
  }
}

void RogueStackTraceFrameList__init_object( RogueStackTraceFrameList* THISOBJ )
{
  {
    RogueObject__init_object(((RogueObject*)THISOBJ));
    {
      {
        THISOBJ->element_size = sizeof(THISOBJ->element_type);
      }
    }
  }
}

void RogueStackTraceFrameList__on_cleanup( RogueStackTraceFrameList* THISOBJ )
{
  {
    if (THISOBJ->as_bytes)
    {
      if (THISOBJ->is_borrowed) THISOBJ->is_borrowed = 0;
      else              ROGUE_FREE( THISOBJ->as_bytes );
      THISOBJ->as_bytes = 0;
      THISOBJ->capacity = 0;
      THISOBJ->count = 0;
    }
  }
}

void RogueStackTraceFrameList__add__RogueStackTraceFrame( RogueStackTraceFrameList* THISOBJ, RogueStackTraceFrame value_0 )
{
  {
    RogueStackTraceFrameList__reserve__RogueInt32( THISOBJ, 1 );
    {
      {
        ((RogueStackTraceFrame*)(THISOBJ->data))[THISOBJ->count++] = value_0;
      }
    }
  }
}

void RogueStackTraceFrameList__clear( RogueStackTraceFrameList* THISOBJ )
{
  {
    RogueStackTraceFrameList__discard_from__RogueInt32( THISOBJ, 0 );
  }
}

RogueString* RogueStackTraceFrameList__description( RogueStackTraceFrameList* THISOBJ )
{
  RogueString* result_0 = 0;
  (void)result_0;
  RogueInt32 i_1 = 0;
  (void)i_1;
  RogueStackTraceFrameList* _auto_collection_0_2 = 0;
  (void)_auto_collection_0_2;
  RogueInt32 _auto_count_1_3 = 0;
  (void)_auto_count_1_3;
  RogueStackTraceFrame value_4 = {0};
  (void)value_4;
  RogueLogical _auto_condition_0_5 = 0;
  (void)_auto_condition_0_5;
  RogueString* _auto_anchored_arg_0_1_6 = 0;
  (void)_auto_anchored_arg_0_1_6;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;
  RogueInt32 _auto_local_pointer_fp_1 = TypeRogueStackTraceFrame.local_pointer_count;

  {
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &result_0 );
    result_0 = ROGUE_CREATE_OBJECT( RogueString );
    RogueString__init(result_0);
    RogueString__print__RogueCharacter( result_0, '[' );
    i_1 = 0;
    _auto_collection_0_2 = 0;
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_collection_0_2 );
    _auto_collection_0_2 = THISOBJ;
    _auto_count_1_3 = 0;
    _auto_count_1_3 = _auto_collection_0_2->count;
    value_4 = (RogueStackTraceFrame){0};
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueStackTraceFrame, &value_4 );
    _auto_condition_0_5 = i_1 < _auto_count_1_3;
    goto _auto_loop_condition_0;
    do
    {
      {
        value_4 = RogueStackTraceFrameList__get__RogueInt32( _auto_collection_0_2, i_1 );
        if (i_1 > 0)
        {
          {
            RogueString__print__RogueCharacter( result_0, ',' );
          }
        }
        _auto_anchored_arg_0_1_6 = 0;
        RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_arg_0_1_6 );
        RogueString__print__RogueString( result_0, (_auto_anchored_arg_0_1_6=RogueStackTraceFrame__toxRogueStringx(value_4)) );
      }
      {
        ++i_1;
        _auto_condition_0_5 = i_1 < _auto_count_1_3;
      }
      _auto_loop_condition_0:;
    }
    while (_auto_condition_0_5);
    RogueString__print__RogueCharacter( result_0, ']' );
    TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
    TypeRogueStackTraceFrame.local_pointer_count = _auto_local_pointer_fp_1;
    return result_0;
  }
}

void RogueStackTraceFrameList__discard_from__RogueInt32( RogueStackTraceFrameList* THISOBJ, RogueInt32 index_0 )
{
  RogueInt32 n_1 = 0;
  (void)n_1;

  {
    n_1 = THISOBJ->count - index_0;
    if (n_1 > 0)
    {
      {
        RogueStackTraceFrameList__zero__RogueInt32_RogueOptionalInt32( THISOBJ, index_0, (RogueOptionalInt32) {n_1,1} );
        THISOBJ->count = index_0;
      }
    }
  }
}

RogueStackTraceFrame RogueStackTraceFrameList__get__RogueInt32( RogueStackTraceFrameList* THISOBJ, RogueInt32 index_0 )
{
  {
    {
      {
        RogueStackTraceFrame _auto_result_0 = ((RogueStackTraceFrame*)(THISOBJ->data))[index_0];
        return _auto_result_0;
      }
    }
  }
}

RogueStackTraceFrame RogueStackTraceFrameList__last( RogueStackTraceFrameList* THISOBJ )
{
  {
    RogueStackTraceFrame _auto_result_0 = RogueStackTraceFrameList__get__RogueInt32( THISOBJ, THISOBJ->count - 1 );
    return _auto_result_0;
  }
}

void RogueStackTraceFrameList__on_return_to_pool( RogueStackTraceFrameList* THISOBJ )
{
  {
    RogueStackTraceFrameList__clear(THISOBJ);
  }
}

RogueStackTraceFrame RogueStackTraceFrameList__remove_last( RogueStackTraceFrameList* THISOBJ )
{
  {
    --THISOBJ->count;
    {
      RogueStackTraceFrame result = ((RogueStackTraceFrame*)(THISOBJ->data))[THISOBJ->count];
      memset( ((RogueStackTraceFrame*)(THISOBJ->data)) + THISOBJ->count, 0, sizeof(RogueStackTraceFrame) );
      RogueStackTraceFrame _auto_result_0 = result;
      return _auto_result_0;
    }
  }
}

void RogueStackTraceFrameList__reserve__RogueInt32( RogueStackTraceFrameList* THISOBJ, RogueInt32 additional_capacity_0 )
{
  RogueInt32 required_capacity_1 = 0;
  (void)required_capacity_1;

  {
    required_capacity_1 = (THISOBJ->count + additional_capacity_0);
    if (required_capacity_1 <= THISOBJ->capacity)
    {
      {
        return;
      }
    }
    required_capacity_1 = RogueInt32__or_larger__RogueInt32( RogueInt32__or_larger__RogueInt32( required_capacity_1, THISOBJ->count * 2 ), 10 );
    int total_size = required_capacity_1 * THISOBJ->element_size;
    RogueMM_bytes_allocated_since_gc += total_size;
    RogueByte* new_data = (RogueByte*) ROGUE_MALLOC( total_size );

    if (THISOBJ->as_bytes)
    {
      int old_size = THISOBJ->capacity * THISOBJ->element_size;
      RogueByte* old_data = THISOBJ->as_bytes;
      memcpy( new_data, old_data, old_size );
      memset( new_data+old_size, 0, total_size-old_size );
      if (THISOBJ->is_borrowed) THISOBJ->is_borrowed = 0;
      else              ROGUE_FREE( old_data );
    }
    else
    {
      memset( new_data, 0, total_size );
    }
    THISOBJ->as_bytes = new_data;
    THISOBJ->capacity = required_capacity_1;
  }
}

RogueString* RogueStackTraceFrameList__toxRogueStringx( RogueStackTraceFrameList* THISOBJ )
{
  {
    RogueString* _auto_result_0 = (RogueString*)RogueStackTraceFrameList__description(THISOBJ);
    return _auto_result_0;
  }
}

void RogueStackTraceFrameList__zero__RogueInt32_RogueOptionalInt32( RogueStackTraceFrameList* THISOBJ, RogueInt32 i1_0, RogueOptionalInt32 count_1 )
{
  RogueInt32 n_2 = 0;
  (void)n_2;

  {
    n_2 = (count_1.exists ? count_1.value : THISOBJ->count);
    memset( THISOBJ->as_bytes + i1_0*THISOBJ->element_size, 0, n_2*THISOBJ->element_size );
  }
}

RogueString* RogueStackTraceFrameList__type_name( RogueStackTraceFrameList* THISOBJ )
{
  {
    RogueString* _auto_result_0 = (RogueString*)RogueObject____type_name__RogueInt32( 25 );
    return _auto_result_0;
  }
}

void RogueStackTrace_gc_trace( void* THISOBJ )
{
  ((RogueObject*)THISOBJ)->__refcount = ~((RogueObject*)THISOBJ)->__refcount;

  RogueObject* ref;
  if ((ref = (RogueObject*)((RogueStackTrace*)THISOBJ)->frames) && ref->__refcount >= 0) ref->__type->fn_gc_trace(ref);
}

void RogueStackTrace__init( RogueStackTrace* THISOBJ )
{
  RogueInt32 n_0 = 0;
  (void)n_0;
  RogueRangeUpToLessThanIteratorxRogueInt32x _auto_collection_0_1 = {0};
  (void)_auto_collection_0_1;
  RogueOptionalInt32 _auto_next_1_2 = {0};
  (void)_auto_next_1_2;
  RogueInt32 i_3 = 0;
  (void)i_3;
  RogueInt32 line_4 = 0;
  (void)line_4;
  RogueStackTraceFrameList* _auto_obj_0_5 = 0;
  (void)_auto_obj_0_5;
  RogueLogical _auto_condition_1_6 = 0;
  (void)_auto_condition_1_6;
  RogueStackTraceFrameList* _auto_anchored_context_2_7 = 0;
  (void)_auto_anchored_context_2_7;
  RogueStackTraceFrame _auto_anchored_arg_0_3_8 = {0};
  (void)_auto_anchored_arg_0_3_8;
  RogueStackTraceFrameList* _auto_anchored_context_4_9 = 0;
  (void)_auto_anchored_context_4_9;
  RogueString* _auto_anchored_arg_0_5_10 = 0;
  (void)_auto_anchored_arg_0_5_10;
  RogueStackTraceFrameList* _auto_anchored_context_6_11 = 0;
  (void)_auto_anchored_context_6_11;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;
  RogueInt32 _auto_local_pointer_fp_1 = TypeRogueStackTraceFrame.local_pointer_count;
  (void)_auto_local_pointer_fp_0;
  (void)_auto_local_pointer_fp_1;

  {
    n_0 = Rogue_call_stack_count;
    if (!!n_0)
    {
      {
        Rogue_call_stack[Rogue_call_stack_count-1].line = Rogue_call_stack_line;
        RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_obj_0_5 );
        _auto_obj_0_5 = ROGUE_CREATE_OBJECT( RogueStackTraceFrameList );
        RogueStackTraceFrameList__init__RogueInt32( _auto_obj_0_5, n_0 );
        THISOBJ->frames = _auto_obj_0_5;
        _auto_collection_0_1 = (RogueRangeUpToLessThanIteratorxRogueInt32x){0};
        _auto_collection_0_1 = RogueRangeUpToLessThanxRogueInt32x__iterator((RogueRangeUpToLessThanxRogueInt32x) {0,n_0,1});
        _auto_next_1_2 = (RogueOptionalInt32){0};
        _auto_next_1_2 = RogueRangeUpToLessThanIteratorxRogueInt32x__read_another(&_auto_collection_0_1);
        i_3 = 0;
        _auto_condition_1_6 = _auto_next_1_2.exists;
        goto _auto_loop_condition_0;
        do
        {
          {
            i_3 = _auto_next_1_2.value;
            line_4 = RogueStackTrace__line__RogueInt32( THISOBJ, i_3 );
            if (line_4 == -1)
            {
              {
                goto _auto_escape_1;
              }
            }
            RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_2_7 );
            _auto_anchored_arg_0_3_8 = (RogueStackTraceFrame){0};
            RogueRuntimeType_local_pointer_stack_add( &TypeRogueStackTraceFrame, &_auto_anchored_arg_0_3_8 );
            RogueStackTraceFrameList__add__RogueStackTraceFrame( (_auto_anchored_context_2_7=THISOBJ->frames), (_auto_anchored_arg_0_3_8=(RogueStackTraceFrame) {RogueStackTrace__procedure__RogueInt32( THISOBJ, i_3 ),RogueStackTrace__filename__RogueInt32( THISOBJ, i_3 ),line_4}) );
          }
          {
            _auto_next_1_2 = RogueRangeUpToLessThanIteratorxRogueInt32x__read_another(&_auto_collection_0_1);
            _auto_condition_1_6 = _auto_next_1_2.exists;
          }
          _auto_loop_condition_0:;
        }
        while (_auto_condition_1_6);
        _auto_escape_1:;
        RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_4_9 );
        _auto_anchored_arg_0_5_10 = 0;
        RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_arg_0_5_10 );
        if (RogueString__operator____RogueString_RogueString( (_auto_anchored_arg_0_5_10=RogueStackTraceFrameList__last((_auto_anchored_context_4_9=THISOBJ->frames)).procedure), str_StackTrace_init__ ))
        {
          {
            RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_6_11 );
            RogueStackTraceFrameList__remove_last((_auto_anchored_context_6_11=THISOBJ->frames));
          }
        }
      }
    }
  }
  TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
  TypeRogueStackTraceFrame.local_pointer_count = _auto_local_pointer_fp_1;
}

RogueString* RogueStackTrace__filename__RogueInt32( RogueStackTrace* THISOBJ, RogueInt32 index_0 )
{
  {
    if (!Rogue_call_stack[index_0].filename)
    {
      {
        return str_INTERNAL;
      }
    }
    RogueString* _auto_result_0 = (RogueString*)RogueString_create( Rogue_call_stack[index_0].filename );
    return _auto_result_0;
  }
}

RogueInt32 RogueStackTrace__line__RogueInt32( RogueStackTrace* THISOBJ, RogueInt32 index_0 )
{
  {
    RogueInt32 _auto_result_0 = Rogue_call_stack[index_0].line;
    return _auto_result_0;
  }
}

RogueString* RogueStackTrace__procedure__RogueInt32( RogueStackTrace* THISOBJ, RogueInt32 index_0 )
{
  {
    if (!Rogue_call_stack[index_0].procedure)
    {
      {
        return str_Unknown_Procedure;
      }
    }
    RogueString* _auto_result_0 = (RogueString*)RogueString_create( Rogue_call_stack[index_0].procedure );
    return _auto_result_0;
  }
}

RogueString* RogueStackTrace__toxRogueStringx( RogueStackTrace* THISOBJ )
{
  RogueInt32 left_w_0 = 0;
  (void)left_w_0;
  RogueInt32 right_w_1 = 0;
  (void)right_w_1;
  RogueInt32 _auto_index_0_2 = 0;
  (void)_auto_index_0_2;
  RogueStackTraceFrameList* _auto_collection_1_3 = 0;
  (void)_auto_collection_1_3;
  RogueInt32 _auto_count_2_4 = 0;
  (void)_auto_count_2_4;
  RogueStackTraceFrame frame_5 = {0};
  (void)frame_5;
  RogueInt32 max_w_6 = 0;
  (void)max_w_6;
  RogueString* result_7 = 0;
  (void)result_7;
  RogueInt32 _auto_index_4_8 = 0;
  (void)_auto_index_4_8;
  RogueStackTraceFrameList* _auto_collection_5_9 = 0;
  (void)_auto_collection_5_9;
  RogueInt32 _auto_count_6_10 = 0;
  (void)_auto_count_6_10;
  RogueStackTraceFrame frame_11 = {0};
  (void)frame_11;
  RogueLogical _auto_condition_0_12 = 0;
  (void)_auto_condition_0_12;
  RogueString* _auto_anchored_context_1_13 = 0;
  (void)_auto_anchored_context_1_13;
  RogueString* _auto_anchored_context_2_14 = 0;
  (void)_auto_anchored_context_2_14;
  RogueLogical _auto_condition_3_15 = 0;
  (void)_auto_condition_3_15;
  RogueString* _auto_anchored_arg_0_4_16 = 0;
  (void)_auto_anchored_arg_0_4_16;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;
  RogueInt32 _auto_local_pointer_fp_1 = TypeRogueStackTraceFrame.local_pointer_count;

  {
    if (!THISOBJ->frames)
    {
      {
        TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
        TypeRogueStackTraceFrame.local_pointer_count = _auto_local_pointer_fp_1;
        return str__Call_history_unavai;
      }
    }
    left_w_0 = 0;
    right_w_1 = 0;
    _auto_index_0_2 = 0;
    _auto_collection_1_3 = 0;
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_collection_1_3 );
    _auto_collection_1_3 = THISOBJ->frames;
    _auto_count_2_4 = 0;
    _auto_count_2_4 = _auto_collection_1_3->count;
    frame_5 = (RogueStackTraceFrame){0};
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueStackTraceFrame, &frame_5 );
    _auto_condition_0_12 = _auto_index_0_2 < _auto_count_2_4;
    goto _auto_loop_condition_0;
    do
    {
      {
        frame_5 = RogueStackTraceFrameList__get__RogueInt32( _auto_collection_1_3, _auto_index_0_2 );
        RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_1_13 );
        left_w_0 = RogueInt32__or_larger__RogueInt32( left_w_0, RogueString__count((_auto_anchored_context_1_13=frame_5.procedure)) );
        RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_2_14 );
        right_w_1 = RogueInt32__or_larger__RogueInt32( right_w_1, (RogueString__count((_auto_anchored_context_2_14=frame_5.filename)) + RogueInt32__digit_count(frame_5.line)) );
      }
      {
        ++_auto_index_0_2;
        _auto_condition_0_12 = _auto_index_0_2 < _auto_count_2_4;
      }
      _auto_loop_condition_0:;
    }
    while (_auto_condition_0_12);
    max_w_6 = RogueInt32__or_larger__RogueInt32( RogueConsole__width(ROGUE_SINGLETON(RogueConsole)), 40 );
    if (((((left_w_0 + right_w_1)) + 5)) > max_w_6)
    {
      {
        left_w_0 = RogueInt32__clamped_low__RogueInt32( max_w_6 - ((right_w_1 + 5)), 20 );
        right_w_1 = max_w_6 - ((left_w_0 + 5));
      }
    }
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &result_7 );
    result_7 = ROGUE_CREATE_OBJECT( RogueString );
    RogueString__init(result_7);
    _auto_index_4_8 = 0;
    _auto_collection_5_9 = 0;
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_collection_5_9 );
    _auto_collection_5_9 = THISOBJ->frames;
    _auto_count_6_10 = 0;
    _auto_count_6_10 = _auto_collection_5_9->count;
    frame_11 = (RogueStackTraceFrame){0};
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueStackTraceFrame, &frame_11 );
    _auto_condition_3_15 = _auto_index_4_8 < _auto_count_6_10;
    goto _auto_loop_condition_1;
    do
    {
      {
        frame_11 = RogueStackTraceFrameList__get__RogueInt32( _auto_collection_5_9, _auto_index_4_8 );
        _auto_anchored_arg_0_4_16 = 0;
        RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_arg_0_4_16 );
        RogueString__println__RogueString( result_7, (_auto_anchored_arg_0_4_16=RogueStackTraceFrame__toxRogueStringx__RogueInt32_RogueInt32( frame_11, left_w_0, right_w_1 )) );
      }
      {
        ++_auto_index_4_8;
        _auto_condition_3_15 = _auto_index_4_8 < _auto_count_6_10;
      }
      _auto_loop_condition_1:;
    }
    while (_auto_condition_3_15);
    TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
    TypeRogueStackTraceFrame.local_pointer_count = _auto_local_pointer_fp_1;
    return result_7;
  }
}

void RogueStackTrace__init_object( RogueStackTrace* THISOBJ )
{
  {
    RogueObject__init_object(((RogueObject*)THISOBJ));
  }
}

RogueString* RogueStackTrace__type_name( RogueStackTrace* THISOBJ )
{
  {
    RogueString* _auto_result_0 = (RogueString*)RogueObject____type_name__RogueInt32( 26 );
    return _auto_result_0;
  }
}

void RogueException_gc_trace( void* THISOBJ )
{
  ((RogueObject*)THISOBJ)->__refcount = ~((RogueObject*)THISOBJ)->__refcount;

  RogueObject* ref;
  if ((ref = (RogueObject*)((RogueException*)THISOBJ)->message) && ref->__refcount >= 0) ref->__type->fn_gc_trace(ref);
  if ((ref = (RogueObject*)((RogueException*)THISOBJ)->stack_trace) && ref->__refcount >= 0) ref->__type->fn_gc_trace(ref);
}

void RogueException__display__RogueException(RogueException* err_0)
{
  {
    if (!!err_0)
    {
      {
        RogueException__display(err_0);
      }
    }
  }
}

void RogueException__init_object( RogueException* THISOBJ )
{
  {
    RogueObject__init_object(((RogueObject*)THISOBJ));
  }
}

void RogueException__display( RogueException* THISOBJ )
{
  RogueObject* _auto_anchored_context_0_0 = 0;
  (void)_auto_anchored_context_0_0;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_0_0 );
    Rogue_dispatch_println__RogueObject( (_auto_anchored_context_0_0=RogueConsole__error(ROGUE_SINGLETON(RogueConsole))), ((RogueObject*)THISOBJ) );
  }
  TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
}

RogueString* RogueException__toxRogueStringx( RogueException* THISOBJ )
{
  RogueString* result_0 = 0;
  (void)result_0;
  RogueString* _auto_anchored_arg_0_0_1 = 0;
  (void)_auto_anchored_arg_0_0_1;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &result_0 );
    result_0 = ROGUE_CREATE_OBJECT( RogueString );
    RogueString__init(result_0);
    _auto_anchored_arg_0_0_1 = 0;
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_arg_0_0_1 );
    RogueString__print__RogueString( result_0, (_auto_anchored_arg_0_0_1=THISOBJ->message) );
    TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
    return result_0;
  }
}

RogueString* RogueException__type_name( RogueException* THISOBJ )
{
  {
    RogueString* _auto_result_0 = (RogueString*)RogueObject____type_name__RogueInt32( 27 );
    return _auto_result_0;
  }
}

void RogueRoutine_gc_trace( void* THISOBJ )
{
  ((RogueObject*)THISOBJ)->__refcount = ~((RogueObject*)THISOBJ)->__refcount;
}

void RogueRoutine__on_launch(void)
{
  {
    RogueGlobal__println__RogueString( ROGUE_SINGLETON(RogueGlobal), str_Hello_Rogue_ );
  }
}

void RogueRoutine__init_object( RogueRoutine* THISOBJ )
{
  {
    RogueObject__init_object(((RogueObject*)THISOBJ));
  }
}

RogueString* RogueRoutine__type_name( RogueRoutine* THISOBJ )
{
  {
    RogueString* _auto_result_0 = (RogueString*)RogueObject____type_name__RogueInt32( 28 );
    return _auto_result_0;
  }
}

void RogueCharacterList_gc_trace( void* THISOBJ )
{
  ((RogueObject*)THISOBJ)->__refcount = ~((RogueObject*)THISOBJ)->__refcount;
}

void RogueCharacterList__init( RogueCharacterList* THISOBJ )
{
}

void RogueCharacterList__init_object( RogueCharacterList* THISOBJ )
{
  {
    RogueObject__init_object(((RogueObject*)THISOBJ));
    {
      {
        THISOBJ->element_size = sizeof(THISOBJ->element_type);
      }
    }
  }
}

void RogueCharacterList__on_cleanup( RogueCharacterList* THISOBJ )
{
  {
    if (THISOBJ->as_bytes)
    {
      if (THISOBJ->is_borrowed) THISOBJ->is_borrowed = 0;
      else              ROGUE_FREE( THISOBJ->as_bytes );
      THISOBJ->as_bytes = 0;
      THISOBJ->capacity = 0;
      THISOBJ->count = 0;
    }
  }
}

void RogueCharacterList__add__RogueCharacter( RogueCharacterList* THISOBJ, RogueCharacter value_0 )
{
  {
    RogueCharacterList__reserve__RogueInt32( THISOBJ, 1 );
    {
      {
        ((RogueCharacter*)(THISOBJ->data))[THISOBJ->count++] = value_0;
      }
    }
  }
}

void RogueCharacterList__clear( RogueCharacterList* THISOBJ )
{
  {
    RogueCharacterList__discard_from__RogueInt32( THISOBJ, 0 );
  }
}

RogueString* RogueCharacterList__description( RogueCharacterList* THISOBJ )
{
  RogueString* result_0 = 0;
  (void)result_0;
  RogueInt32 i_1 = 0;
  (void)i_1;
  RogueCharacterList* _auto_collection_0_2 = 0;
  (void)_auto_collection_0_2;
  RogueInt32 _auto_count_1_3 = 0;
  (void)_auto_count_1_3;
  RogueCharacter value_4 = 0;
  (void)value_4;
  RogueLogical _auto_condition_0_5 = 0;
  (void)_auto_condition_0_5;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &result_0 );
    result_0 = ROGUE_CREATE_OBJECT( RogueString );
    RogueString__init(result_0);
    RogueString__print__RogueCharacter( result_0, '[' );
    i_1 = 0;
    _auto_collection_0_2 = 0;
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_collection_0_2 );
    _auto_collection_0_2 = THISOBJ;
    _auto_count_1_3 = 0;
    _auto_count_1_3 = _auto_collection_0_2->count;
    value_4 = 0;
    _auto_condition_0_5 = i_1 < _auto_count_1_3;
    goto _auto_loop_condition_0;
    do
    {
      {
        value_4 = RogueCharacterList__get__RogueInt32( _auto_collection_0_2, i_1 );
        if (i_1 > 0)
        {
          {
            RogueString__print__RogueCharacter( result_0, ',' );
          }
        }
        RogueString__print__RogueCharacter( result_0, value_4 );
      }
      {
        ++i_1;
        _auto_condition_0_5 = i_1 < _auto_count_1_3;
      }
      _auto_loop_condition_0:;
    }
    while (_auto_condition_0_5);
    RogueString__print__RogueCharacter( result_0, ']' );
    TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
    return result_0;
  }
}

void RogueCharacterList__discard_from__RogueInt32( RogueCharacterList* THISOBJ, RogueInt32 index_0 )
{
  RogueInt32 n_1 = 0;
  (void)n_1;

  {
    n_1 = THISOBJ->count - index_0;
    if (n_1 > 0)
    {
      {
        RogueCharacterList__zero__RogueInt32_RogueOptionalInt32( THISOBJ, index_0, (RogueOptionalInt32) {n_1,1} );
        THISOBJ->count = index_0;
      }
    }
  }
}

RogueCharacter RogueCharacterList__get__RogueInt32( RogueCharacterList* THISOBJ, RogueInt32 index_0 )
{
  {
    {
      {
        RogueCharacter _auto_result_0 = ((RogueCharacter*)(THISOBJ->data))[index_0];
        return _auto_result_0;
      }
    }
  }
}

void RogueCharacterList__on_return_to_pool( RogueCharacterList* THISOBJ )
{
  {
    RogueCharacterList__clear(THISOBJ);
  }
}

void RogueCharacterList__reserve__RogueInt32( RogueCharacterList* THISOBJ, RogueInt32 additional_capacity_0 )
{
  RogueInt32 required_capacity_1 = 0;
  (void)required_capacity_1;

  {
    required_capacity_1 = (THISOBJ->count + additional_capacity_0);
    if (required_capacity_1 <= THISOBJ->capacity)
    {
      {
        return;
      }
    }
    required_capacity_1 = RogueInt32__or_larger__RogueInt32( RogueInt32__or_larger__RogueInt32( required_capacity_1, THISOBJ->count * 2 ), 10 );
    int total_size = required_capacity_1 * THISOBJ->element_size;
    RogueMM_bytes_allocated_since_gc += total_size;
    RogueByte* new_data = (RogueByte*) ROGUE_MALLOC( total_size );

    if (THISOBJ->as_bytes)
    {
      int old_size = THISOBJ->capacity * THISOBJ->element_size;
      RogueByte* old_data = THISOBJ->as_bytes;
      memcpy( new_data, old_data, old_size );
      memset( new_data+old_size, 0, total_size-old_size );
      if (THISOBJ->is_borrowed) THISOBJ->is_borrowed = 0;
      else              ROGUE_FREE( old_data );
    }
    else
    {
      memset( new_data, 0, total_size );
    }
    THISOBJ->as_bytes = new_data;
    THISOBJ->capacity = required_capacity_1;
  }
}

RogueString* RogueCharacterList__toxRogueStringx( RogueCharacterList* THISOBJ )
{
  {
    RogueString* _auto_result_0 = (RogueString*)RogueCharacterList__description(THISOBJ);
    return _auto_result_0;
  }
}

void RogueCharacterList__zero__RogueInt32_RogueOptionalInt32( RogueCharacterList* THISOBJ, RogueInt32 i1_0, RogueOptionalInt32 count_1 )
{
  RogueInt32 n_2 = 0;
  (void)n_2;

  {
    n_2 = (count_1.exists ? count_1.value : THISOBJ->count);
    memset( THISOBJ->as_bytes + i1_0*THISOBJ->element_size, 0, n_2*THISOBJ->element_size );
  }
}

RogueString* RogueCharacterList__type_name( RogueCharacterList* THISOBJ )
{
  {
    RogueString* _auto_result_0 = (RogueString*)RogueObject____type_name__RogueInt32( 35 );
    return _auto_result_0;
  }
}

void RogueStringList_gc_trace( void* THISOBJ )
{
  ((RogueObject*)THISOBJ)->__refcount = ~((RogueObject*)THISOBJ)->__refcount;

  RogueObject* ref;
  if ((ref = (RogueObject*)((RogueStringList*)THISOBJ)->element_type) && ref->__refcount >= 0) ref->__type->fn_gc_trace(ref);

  RogueObject** cursor = ((RogueObject**)((RogueStringList*)THISOBJ)->data) - 1;
  int n = ((int)((RogueStringList*)THISOBJ)->capacity);
  while (--n >=0)
  {
    if ((ref = *(++cursor)) && ref->__refcount >= 0) ref->__type->fn_gc_trace(ref);
  }
}

void RogueStringList__init( RogueStringList* THISOBJ )
{
}

void RogueStringList__init_object( RogueStringList* THISOBJ )
{
  {
    RogueObject__init_object(((RogueObject*)THISOBJ));
    {
      {
        THISOBJ->element_size = sizeof(THISOBJ->element_type);
      }
    }
    {
      THISOBJ->is_ref_array = 1;
    }
  }
}

void RogueStringList__on_cleanup( RogueStringList* THISOBJ )
{
  {
    if (THISOBJ->as_bytes)
    {
      if (THISOBJ->is_borrowed) THISOBJ->is_borrowed = 0;
      else              ROGUE_FREE( THISOBJ->as_bytes );
      THISOBJ->as_bytes = 0;
      THISOBJ->capacity = 0;
      THISOBJ->count = 0;
    }
  }
}

void RogueStringList__add__RogueString( RogueStringList* THISOBJ, RogueString* value_0 )
{
  {
    RogueStringList__reserve__RogueInt32( THISOBJ, 1 );
    {
      {
        ((RogueString**)(THISOBJ->data))[THISOBJ->count++] = value_0;
      }
    }
  }
}

void RogueStringList__clear( RogueStringList* THISOBJ )
{
  {
    RogueStringList__discard_from__RogueInt32( THISOBJ, 0 );
  }
}

RogueString* RogueStringList__description( RogueStringList* THISOBJ )
{
  RogueString* result_0 = 0;
  (void)result_0;
  RogueInt32 i_1 = 0;
  (void)i_1;
  RogueStringList* _auto_collection_0_2 = 0;
  (void)_auto_collection_0_2;
  RogueInt32 _auto_count_1_3 = 0;
  (void)_auto_count_1_3;
  RogueString* value_4 = 0;
  (void)value_4;
  RogueLogical _auto_condition_0_5 = 0;
  (void)_auto_condition_0_5;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &result_0 );
    result_0 = ROGUE_CREATE_OBJECT( RogueString );
    RogueString__init(result_0);
    RogueString__print__RogueCharacter( result_0, '[' );
    i_1 = 0;
    _auto_collection_0_2 = 0;
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_collection_0_2 );
    _auto_collection_0_2 = THISOBJ;
    _auto_count_1_3 = 0;
    _auto_count_1_3 = _auto_collection_0_2->count;
    value_4 = 0;
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &value_4 );
    _auto_condition_0_5 = i_1 < _auto_count_1_3;
    goto _auto_loop_condition_0;
    do
    {
      {
        value_4 = RogueStringList__get__RogueInt32( _auto_collection_0_2, i_1 );
        if (i_1 > 0)
        {
          {
            RogueString__print__RogueCharacter( result_0, ',' );
          }
        }
        RogueString__print__RogueString( result_0, value_4 );
      }
      {
        ++i_1;
        _auto_condition_0_5 = i_1 < _auto_count_1_3;
      }
      _auto_loop_condition_0:;
    }
    while (_auto_condition_0_5);
    RogueString__print__RogueCharacter( result_0, ']' );
    TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
    return result_0;
  }
}

void RogueStringList__discard_from__RogueInt32( RogueStringList* THISOBJ, RogueInt32 index_0 )
{
  RogueInt32 n_1 = 0;
  (void)n_1;

  {
    n_1 = THISOBJ->count - index_0;
    if (n_1 > 0)
    {
      {
        RogueStringList__zero__RogueInt32_RogueOptionalInt32( THISOBJ, index_0, (RogueOptionalInt32) {n_1,1} );
        THISOBJ->count = index_0;
      }
    }
  }
}

RogueString* RogueStringList__get__RogueInt32( RogueStringList* THISOBJ, RogueInt32 index_0 )
{
  {
    {
      {
        RogueString* _auto_result_0 = (RogueString*)((RogueString**)(THISOBJ->data))[index_0];
        return _auto_result_0;
      }
    }
  }
}

RogueLogical RogueStringList__is_empty( RogueStringList* THISOBJ )
{
  {
    return THISOBJ->count == 0;
  }
}

void RogueStringList__on_return_to_pool( RogueStringList* THISOBJ )
{
  {
    RogueStringList__clear(THISOBJ);
  }
}

RogueString* RogueStringList__remove_last( RogueStringList* THISOBJ )
{
  {
    --THISOBJ->count;
    {
      {
        RogueString* result = ((RogueString**)(THISOBJ->data))[THISOBJ->count];
        ((RogueString**)(THISOBJ->data))[THISOBJ->count] = 0;
        RogueString* _auto_result_0 = (RogueString*)result;
        return _auto_result_0;
      }
    }
  }
}

void RogueStringList__reserve__RogueInt32( RogueStringList* THISOBJ, RogueInt32 additional_capacity_0 )
{
  RogueInt32 required_capacity_1 = 0;
  (void)required_capacity_1;

  {
    required_capacity_1 = (THISOBJ->count + additional_capacity_0);
    if (required_capacity_1 <= THISOBJ->capacity)
    {
      {
        return;
      }
    }
    required_capacity_1 = RogueInt32__or_larger__RogueInt32( RogueInt32__or_larger__RogueInt32( required_capacity_1, THISOBJ->count * 2 ), 10 );
    int total_size = required_capacity_1 * THISOBJ->element_size;
    RogueMM_bytes_allocated_since_gc += total_size;
    RogueByte* new_data = (RogueByte*) ROGUE_MALLOC( total_size );

    if (THISOBJ->as_bytes)
    {
      int old_size = THISOBJ->capacity * THISOBJ->element_size;
      RogueByte* old_data = THISOBJ->as_bytes;
      memcpy( new_data, old_data, old_size );
      memset( new_data+old_size, 0, total_size-old_size );
      if (THISOBJ->is_borrowed) THISOBJ->is_borrowed = 0;
      else              ROGUE_FREE( old_data );
    }
    else
    {
      memset( new_data, 0, total_size );
    }
    THISOBJ->as_bytes = new_data;
    THISOBJ->capacity = required_capacity_1;
  }
}

RogueString* RogueStringList__toxRogueStringx( RogueStringList* THISOBJ )
{
  {
    RogueString* _auto_result_0 = (RogueString*)RogueStringList__description(THISOBJ);
    return _auto_result_0;
  }
}

void RogueStringList__zero__RogueInt32_RogueOptionalInt32( RogueStringList* THISOBJ, RogueInt32 i1_0, RogueOptionalInt32 count_1 )
{
  RogueInt32 n_2 = 0;
  (void)n_2;

  {
    n_2 = (count_1.exists ? count_1.value : THISOBJ->count);
    memset( THISOBJ->as_bytes + i1_0*THISOBJ->element_size, 0, n_2*THISOBJ->element_size );
  }
}

RogueString* RogueStringList__type_name( RogueStringList* THISOBJ )
{
  {
    RogueString* _auto_result_0 = (RogueString*)RogueObject____type_name__RogueInt32( 36 );
    return _auto_result_0;
  }
}

void RogueStringPool_gc_trace( void* THISOBJ )
{
  ((RogueObject*)THISOBJ)->__refcount = ~((RogueObject*)THISOBJ)->__refcount;

  RogueObject* ref;
  if ((ref = (RogueObject*)((RogueStringPool*)THISOBJ)->available) && ref->__refcount >= 0) ref->__type->fn_gc_trace(ref);
}

void RogueStringPool__init_object( RogueStringPool* THISOBJ )
{
  {
    RogueObjectPoolxRogueStringx__init_object(((RogueObjectPoolxRogueStringx*)THISOBJ));
  }
}

RogueString* RogueStringPool__type_name( RogueStringPool* THISOBJ )
{
  {
    RogueString* _auto_result_0 = (RogueString*)RogueObject____type_name__RogueInt32( 38 );
    return _auto_result_0;
  }
}

void RogueSystem_gc_trace( void* THISOBJ )
{
  ((RogueObject*)THISOBJ)->__refcount = ~((RogueObject*)THISOBJ)->__refcount;
}

void RogueSystem__exit__RogueInt32(RogueInt32 result_code_0)
{
  {
    Rogue_quit();
    #if defined(ROGUE_GC_AUTO)
      Rogue_clean_up();
    #endif
    Rogue_exit( result_code_0 );
  }
}

RogueLogical RogueSystem__is_windows(void)
{
  {
    #if defined(ROGUE_PLATFORM_WINDOWS)
    RogueLogical _auto_result_0 = !!1;
    return _auto_result_0;
    #else
    RogueLogical _auto_result_1 = !!0;
    return _auto_result_1;
    #endif
  }
}

RogueInt64 RogueSystem__time_ms(void)
{
  RogueInt64 time_seconds_x1000_0 = 0;
  (void)time_seconds_x1000_0;

  {
    time_seconds_x1000_0 = 0;
    #if defined(ROGUE_PLATFORM_WINDOWS)
      struct __timeb64 time_struct;
      _ftime64_s( &time_struct );
      time_seconds_x1000_0 = ((RogueInt64) time_struct.time) * 1000;
      time_seconds_x1000_0 += time_struct.millitm;

    #else
      struct timeval time_struct;
      gettimeofday( &time_struct, 0 );
      time_seconds_x1000_0 = ((RogueInt64) time_struct.tv_sec) * 1000;
      time_seconds_x1000_0 += (time_struct.tv_usec / 1000);
    #endif
    return time_seconds_x1000_0;
  }
}

void RogueSystem___add_command_line_argument__RogueString(RogueString* arg_0)
{
  RogueStringList* _auto_anchored_context_0_1 = 0;
  (void)_auto_anchored_context_0_1;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_0_1 );
    RogueStringList__add__RogueString( (_auto_anchored_context_0_1=RogueSystem__g_command_line_arguments), arg_0 );
  }
  TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
}

void RogueSystem__init_class(void)
{
  RogueStringList* _auto_obj_0_0 = 0;
  (void)_auto_obj_0_0;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_obj_0_0 );
    _auto_obj_0_0 = ROGUE_CREATE_OBJECT( RogueStringList );
    RogueStringList__init(_auto_obj_0_0);
    RogueSystem__g_command_line_arguments = _auto_obj_0_0;
    RogueSystem__g_executable_filepath = 0;
    RogueSystem__g_execution_start_ms = RogueSystem__time_ms() - ((RogueInt64)1);
  }
  TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
}

void RogueSystem__init_object( RogueSystem* THISOBJ )
{
  {
    RogueObject__init_object(((RogueObject*)THISOBJ));
  }
}

RogueString* RogueSystem__type_name( RogueSystem* THISOBJ )
{
  {
    RogueString* _auto_result_0 = (RogueString*)RogueObject____type_name__RogueInt32( 51 );
    return _auto_result_0;
  }
}

void RogueConsoleMode_gc_trace( void* THISOBJ )
{
  ((RogueObject*)THISOBJ)->__refcount = ~((RogueObject*)THISOBJ)->__refcount;
}

void RogueConsoleMode__init_class(void)
{
  {
    RogueConsoleMode__g_configured_on_exit = 0;
  }
}

void RogueConsoleMode__init_object( RogueConsoleMode* THISOBJ )
{
  {
    RogueObject__init_object(((RogueObject*)THISOBJ));
    if (!RogueConsoleMode__g_configured_on_exit)
    {
      {
        RogueConsoleMode__g_configured_on_exit = 1;
        RogueGlobal__on_exit__RogueOPARENFunctionOPARENCPARENCPAREN( ROGUE_SINGLETON(RogueGlobal), ((RogueOPARENFunctionOPARENCPARENCPAREN*)ROGUE_SINGLETON(RogueFunction_260)) );
      }
    }
  }
}

void RogueConsoleMode___on_enter( RogueConsoleMode* THISOBJ )
{
}

void RogueConsoleMode___on_exit( RogueConsoleMode* THISOBJ )
{
}

RogueString* RogueConsoleMode__type_name( RogueConsoleMode* THISOBJ )
{
  {
    RogueString* _auto_result_0 = (RogueString*)RogueObject____type_name__RogueInt32( 52 );
    return _auto_result_0;
  }
}

void RogueConsole_gc_trace( void* THISOBJ )
{
  ((RogueObject*)THISOBJ)->__refcount = ~((RogueObject*)THISOBJ)->__refcount;

  RogueObject* ref;
  if ((ref = (RogueObject*)((RogueConsole*)THISOBJ)->output_buffer) && ref->__refcount >= 0) ref->__type->fn_gc_trace(ref);
  if ((ref = (RogueObject*)((RogueConsole*)THISOBJ)->error) && ref->__refcount >= 0) ref->__type->fn_gc_trace(ref);
  if ((ref = (RogueObject*)((RogueConsole*)THISOBJ)->mode) && ref->__refcount >= 0) ref->__type->fn_gc_trace(ref);
  if ((ref = (RogueObject*)((RogueConsole*)THISOBJ)->input_buffer) && ref->__refcount >= 0) ref->__type->fn_gc_trace(ref);
  if ((ref = (RogueObject*)((RogueConsole*)THISOBJ)->_input_bytes) && ref->__refcount >= 0) ref->__type->fn_gc_trace(ref);
}

void RogueConsole__init( RogueConsole* THISOBJ )
{
  {
    #ifdef ROGUE_PLATFORM_WINDOWS
      // Enable ANSI colors and styles on Windows
      HANDLE h_stdout = GetStdHandle( STD_OUTPUT_HANDLE );
      DWORD mode;
      GetConsoleMode( h_stdout, &mode );
      SetConsoleMode( h_stdout, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING );
      SetConsoleOutputCP(65001);  // Extended characters

      HANDLE h_stdin = GetStdHandle( STD_INPUT_HANDLE );
      GetConsoleMode( h_stdin, &mode );
    THISOBJ->windows_in_quick_edit_mode = !!(mode & ENABLE_QUICK_EDIT_MODE);
    #elif !defined(ROGUE_PLATFORM_EMBEDDED)
      tcgetattr( STDIN_FILENO, &THISOBJ->original_terminal_settings );
      THISOBJ->original_stdin_flags = fcntl( STDIN_FILENO, F_GETFL );

      if ( !THISOBJ->original_terminal_settings.c_cc[VMIN] ) THISOBJ->original_terminal_settings.c_cc[VMIN] = 1;
      THISOBJ->original_terminal_settings.c_lflag |= (ECHO | ECHOE | ICANON);
      THISOBJ->original_stdin_flags &= ~(O_NONBLOCK);
    #endif
  }
}

RogueObject* RogueConsole__error( RogueConsole* THISOBJ )
{
  RogueConsoleErrorPrinter* _auto_obj_0_0 = 0;
  (void)_auto_obj_0_0;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    if (!!THISOBJ->error)
    {
      {
        TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
        return THISOBJ->error;
      }
    }
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_obj_0_0 );
    _auto_obj_0_0 = ROGUE_CREATE_OBJECT( RogueConsoleErrorPrinter );
    THISOBJ->error = ((RogueObject*)_auto_obj_0_0);
    TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
    return THISOBJ->error;
  }
}

void RogueConsole__flush__RogueString( RogueConsole* THISOBJ, RogueString* buffer_0 )
{
  {
    RogueConsole__write__RogueString( THISOBJ, buffer_0 );
    RogueString__clear(buffer_0);
  }
}

RogueLogical RogueConsole__has_another( RogueConsole* THISOBJ )
{
  RogueObject* _auto_virtual_context_0;
  (void) _auto_virtual_context_0;
  {
    RogueLogical _auto_result_0 = ((void)(_auto_virtual_context_0=(RogueObject*)(RogueConsole__mode(THISOBJ))),((RogueLogical (*)(RogueConsoleMode*))(_auto_virtual_context_0->__type->vtable[3]))((RogueConsoleMode*)_auto_virtual_context_0));
    return _auto_result_0;
  }
}

RogueConsoleMode* RogueConsole__mode( RogueConsole* THISOBJ )
{
  RogueStandardConsoleMode* _auto_obj_0_0 = 0;
  (void)_auto_obj_0_0;
  RogueConsoleMode* _auto_anchored_context_1_1 = 0;
  (void)_auto_anchored_context_1_1;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    if (!!THISOBJ->mode)
    {
      {
        TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
        return THISOBJ->mode;
      }
    }
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_obj_0_0 );
    _auto_obj_0_0 = ROGUE_CREATE_OBJECT( RogueStandardConsoleMode );
    RogueStandardConsoleMode__init(_auto_obj_0_0);
    THISOBJ->mode = ((RogueConsoleMode*)_auto_obj_0_0);
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_1_1 );
    RogueConsoleMode___on_enter((_auto_anchored_context_1_1=THISOBJ->mode));
    TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
    return THISOBJ->mode;
  }
}

RogueCharacter RogueConsole__read( RogueConsole* THISOBJ )
{
  RogueObject* _auto_virtual_context_0;
  (void) _auto_virtual_context_0;
  {
    RogueCharacter _auto_result_0 = ((void)(_auto_virtual_context_0=(RogueObject*)(RogueConsole__mode(THISOBJ))),((RogueCharacter (*)(RogueConsoleMode*))(_auto_virtual_context_0->__type->vtable[4]))((RogueConsoleMode*)_auto_virtual_context_0));
    return _auto_result_0;
  }
}

void RogueConsole__set_immediate_mode__RogueLogical( RogueConsole* THISOBJ, RogueLogical setting_0 )
{
  RogueObject* _auto_virtual_context_0;
  (void) _auto_virtual_context_0;
  RogueImmediateConsoleMode* _auto_obj_0_1 = 0;
  (void)_auto_obj_0_1;
  RogueStandardConsoleMode* _auto_obj_1_2 = 0;
  (void)_auto_obj_1_2;
  RogueConsoleMode* _auto_anchored_context_2_3 = 0;
  (void)_auto_anchored_context_2_3;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    if (THISOBJ->immediate_mode != setting_0)
    {
      {
        THISOBJ->immediate_mode = setting_0;
        if (!!THISOBJ->mode)
        {
          {
            ((void)(_auto_virtual_context_0=(RogueObject*)(THISOBJ->mode)),((void (*)(RogueConsoleMode*))(_auto_virtual_context_0->__type->vtable[5]))((RogueConsoleMode*)_auto_virtual_context_0));
          }
        }
        if (THISOBJ->immediate_mode)
        {
          {
            RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_obj_0_1 );
            _auto_obj_0_1 = ROGUE_CREATE_OBJECT( RogueImmediateConsoleMode );
            RogueImmediateConsoleMode__init(_auto_obj_0_1);
            THISOBJ->mode = ((RogueConsoleMode*)_auto_obj_0_1);
          }
        }
        else
        {
          {
            {
              {
                RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_obj_1_2 );
                _auto_obj_1_2 = ROGUE_CREATE_OBJECT( RogueStandardConsoleMode );
                RogueStandardConsoleMode__init(_auto_obj_1_2);
                THISOBJ->mode = ((RogueConsoleMode*)_auto_obj_1_2);
              }
            }
          }
        }
        RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_2_3 );
        RogueConsoleMode___on_enter((_auto_anchored_context_2_3=THISOBJ->mode));
      }
    }
  }
  TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
}

RogueInt32 RogueConsole__width( RogueConsole* THISOBJ )
{
  RogueInt32 result_0 = 0;
  (void)result_0;

  {
    result_0 = 0;
    #ifdef ROGUE_PLATFORM_WINDOWS
      HANDLE h_stdout = GetStdHandle( STD_OUTPUT_HANDLE );
      CONSOLE_SCREEN_BUFFER_INFO info;
      if (GetConsoleScreenBufferInfo(h_stdout,&info)) result_0 = info.dwSize.X;
      else result_0 = 80;
    #else
      struct winsize sz;
      ioctl( STDOUT_FILENO, TIOCGWINSZ, &sz );

      result_0 = sz.ws_col;
    #endif
    return result_0;
  }
}

void RogueConsole__write__RogueString( RogueConsole* THISOBJ, RogueString* value_0 )
{
  {
    #ifdef ROGUE_PLATFORM_ANDROID
      __android_log_print( ANDROID_LOG_INFO, "Rogue", "%s", value_0->data->as_utf8 );
    #else
      Rogue_fwrite( (char*)value_0->data->as_utf8, value_0->data->count, STDOUT_FILENO );
    #endif
  }
}

RogueLogical RogueConsole___fill_input_buffer__RogueInt32( RogueConsole* THISOBJ, RogueInt32 minimum_0 )
{
  RogueLogical needs_unblock_1 = 0;
  (void)needs_unblock_1;
  RogueInt32 n_2 = 0;
  (void)n_2;
  RogueRangeUpToLessThanIteratorxRogueInt32x _auto_collection_1_3 = {0};
  (void)_auto_collection_1_3;
  RogueOptionalInt32 _auto_next_2_4 = {0};
  (void)_auto_next_2_4;
  RogueInt32 i_5 = 0;
  (void)i_5;
  RogueLogical _auto_condition_0_6 = 0;
  (void)_auto_condition_0_6;
  RogueLogical _auto_condition_1_7 = 0;
  (void)_auto_condition_1_7;
  RogueByteList* _auto_anchored_context_2_8 = 0;
  (void)_auto_anchored_context_2_8;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;
  (void)_auto_local_pointer_fp_0;

  {
    if (THISOBJ->_input_bytes->count >= minimum_0)
    {
      {
        TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
        return 1;
      }
    }
    needs_unblock_1 = 0;
    if (THISOBJ->force_input_blocking && THISOBJ->immediate_mode)
    {
      {
        needs_unblock_1 = 1;
        #if !defined(ROGUE_PLATFORM_WINDOWS) && !defined(ROGUE_PLATFORM_EMBEDDED)
          fcntl( STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO,F_GETFL) & ~O_NONBLOCK );

          struct termios settings;
          tcgetattr( STDIN_FILENO, &settings );
          settings.c_cc[VMIN] = 1;
          settings.c_cc[VTIME] = 0;
          tcsetattr( STDIN_FILENO, TCSANOW, &settings );
        #endif
      }
    }
    n_2 = 1024;
    _auto_condition_0_6 = n_2 == 1024;
    goto _auto_loop_condition_0;
    do
    {
      {
        char bytes[1024];
        n_2 = (RogueInt32) ROGUE_READ_CALL( STDIN_FILENO, &bytes, 1024 );
        if (needs_unblock_1)
        {
          {
            #if !defined(ROGUE_PLATFORM_WINDOWS) && !defined(ROGUE_PLATFORM_EMBEDDED)
              fcntl( STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO,F_GETFL)|O_NONBLOCK );

              struct termios settings;
              tcgetattr( STDIN_FILENO, &settings );
              settings.c_cc[VMIN] = 0;
              settings.c_cc[VTIME] = 0;
              tcsetattr( STDIN_FILENO, TCSANOW, &settings );
            #endif
          }
        }
        if ((n_2 == 0) && (needs_unblock_1 || (!THISOBJ->immediate_mode)))
        {
          {
            THISOBJ->is_end_of_input = 1;
            goto _auto_escape_1;
          }
        }
        needs_unblock_1 = 0;
        if (n_2 > 0)
        {
          {
            _auto_collection_1_3 = (RogueRangeUpToLessThanIteratorxRogueInt32x){0};
            _auto_collection_1_3 = RogueRangeUpToLessThanxRogueInt32x__iterator((RogueRangeUpToLessThanxRogueInt32x) {0,n_2,1});
            _auto_next_2_4 = (RogueOptionalInt32){0};
            _auto_next_2_4 = RogueRangeUpToLessThanIteratorxRogueInt32x__read_another(&_auto_collection_1_3);
            i_5 = 0;
            _auto_condition_1_7 = _auto_next_2_4.exists;
            goto _auto_loop_condition_2;
            do
            {
              {
                i_5 = _auto_next_2_4.value;
                RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_2_8 );
                RogueByteList__add__RogueByte( (_auto_anchored_context_2_8=THISOBJ->_input_bytes), ((RogueByte)bytes[i_5]) );
              }
              {
                _auto_next_2_4 = RogueRangeUpToLessThanIteratorxRogueInt32x__read_another(&_auto_collection_1_3);
                _auto_condition_1_7 = _auto_next_2_4.exists;
              }
              _auto_loop_condition_2:;
            }
            while (_auto_condition_1_7);
          }
        }
      }
      {
        _auto_condition_0_6 = n_2 == 1024;
      }
      _auto_loop_condition_0:;
    }
    while (_auto_condition_0_6);
    _auto_escape_1:;
    TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
    return THISOBJ->_input_bytes->count >= minimum_0;
  }
}

void RogueConsole__init_object( RogueConsole* THISOBJ )
{
  RogueString* _auto_obj_0_0 = 0;
  (void)_auto_obj_0_0;
  RogueByteList* _auto_obj_1_1 = 0;
  (void)_auto_obj_1_1;
  RogueByteList* _auto_obj_2_2 = 0;
  (void)_auto_obj_2_2;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    RogueObject__init_object(((RogueObject*)THISOBJ));
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_obj_0_0 );
    _auto_obj_0_0 = ROGUE_CREATE_OBJECT( RogueString );
    RogueString__init(_auto_obj_0_0);
    THISOBJ->output_buffer = _auto_obj_0_0;
    THISOBJ->decode_utf8 = 1;
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_obj_1_1 );
    _auto_obj_1_1 = ROGUE_CREATE_OBJECT( RogueByteList );
    RogueByteList__init(_auto_obj_1_1);
    THISOBJ->input_buffer = _auto_obj_1_1;
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_obj_2_2 );
    _auto_obj_2_2 = ROGUE_CREATE_OBJECT( RogueByteList );
    RogueByteList__init(_auto_obj_2_2);
    THISOBJ->_input_bytes = _auto_obj_2_2;
    THISOBJ->force_input_blocking = 0;
  }
  TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
}

RogueString* RogueConsole__toxRogueStringx( RogueConsole* THISOBJ )
{
  RogueCharacterList* buffer_0 = 0;
  (void)buffer_0;
  RogueLogical _auto_condition_0_1 = 0;
  (void)_auto_condition_0_1;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &buffer_0 );
    buffer_0 = ROGUE_CREATE_OBJECT( RogueCharacterList );
    RogueCharacterList__init(buffer_0);
    _auto_condition_0_1 = RogueConsole__has_another(THISOBJ);
    goto _auto_loop_condition_0;
    do
    {
      {
        RogueCharacterList__add__RogueCharacter( buffer_0, RogueConsole__read(THISOBJ) );
      }
      {
        _auto_condition_0_1 = RogueConsole__has_another(THISOBJ);
      }
      _auto_loop_condition_0:;
    }
    while (_auto_condition_0_1);
    RogueString* _auto_result_1 = (RogueString*)RogueCharacterList__toxRogueStringx(buffer_0);
    TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
    return _auto_result_1;
  }
}

RogueString* RogueConsole__type_name( RogueConsole* THISOBJ )
{
  {
    RogueString* _auto_result_0 = (RogueString*)RogueObject____type_name__RogueInt32( 53 );
    return _auto_result_0;
  }
}

void RogueConsole__flush( RogueConsole* THISOBJ )
{
  RogueString* _auto_anchored_arg_0_0_0 = 0;
  (void)_auto_anchored_arg_0_0_0;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    _auto_anchored_arg_0_0_0 = 0;
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_arg_0_0_0 );
    RogueConsole__flush__RogueString( THISOBJ, (_auto_anchored_arg_0_0_0=THISOBJ->output_buffer) );
  }
  TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
}

void RogueConsole__print__RogueCharacter( RogueConsole* THISOBJ, RogueCharacter value_0 )
{
  RogueString* _auto_anchored_context_0_1 = 0;
  (void)_auto_anchored_context_0_1;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_0_1 );
    RogueString__print__RogueCharacter( (_auto_anchored_context_0_1=THISOBJ->output_buffer), value_0 );
    if (value_0 == ((RogueCharacter)10))
    {
      {
        RogueConsole__flush(THISOBJ);
      }
    }
  }
  TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
}

void RogueConsole__print__RogueObject( RogueConsole* THISOBJ, RogueObject* value_0 )
{
  RogueString* _auto_anchored_context_0_1 = 0;
  (void)_auto_anchored_context_0_1;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_0_1 );
    RogueString__print__RogueObject( (_auto_anchored_context_0_1=THISOBJ->output_buffer), value_0 );
  }
  TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
}

void RogueConsole__print__RogueString( RogueConsole* THISOBJ, RogueString* value_0 )
{
  RogueString* _auto_anchored_context_0_1 = 0;
  (void)_auto_anchored_context_0_1;
  RogueString* _auto_anchored_context_1_2 = 0;
  (void)_auto_anchored_context_1_2;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_0_1 );
    RogueString__print__RogueString( (_auto_anchored_context_0_1=THISOBJ->output_buffer), value_0 );
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_1_2 );
    if (RogueString__count((_auto_anchored_context_1_2=THISOBJ->output_buffer)) > 1024)
    {
      {
        RogueConsole__flush(THISOBJ);
      }
    }
  }
  TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
}

void RogueConsole__println( RogueConsole* THISOBJ )
{
  RogueString* _auto_anchored_context_0_0 = 0;
  (void)_auto_anchored_context_0_0;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_0_0 );
    RogueString__print__RogueCharacter( (_auto_anchored_context_0_0=THISOBJ->output_buffer), ((RogueCharacter)10) );
    RogueConsole__flush(THISOBJ);
  }
  TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
}

void RogueConsole__println__RogueObject( RogueConsole* THISOBJ, RogueObject* value_0 )
{
  {
    RogueConsole__print__RogueObject( THISOBJ, value_0 );
    RogueConsole__println(THISOBJ);
  }
}

void RogueConsole__println__RogueString( RogueConsole* THISOBJ, RogueString* value_0 )
{
  {
    RogueConsole__print__RogueString( THISOBJ, value_0 );
    RogueConsole__println(THISOBJ);
  }
}

void RogueObjectPoolxRogueStringx_gc_trace( void* THISOBJ )
{
  ((RogueObject*)THISOBJ)->__refcount = ~((RogueObject*)THISOBJ)->__refcount;

  RogueObject* ref;
  if ((ref = (RogueObject*)((RogueObjectPoolxRogueStringx*)THISOBJ)->available) && ref->__refcount >= 0) ref->__type->fn_gc_trace(ref);
}

RogueString* RogueObjectPoolxRogueStringx__on_use( RogueObjectPoolxRogueStringx* THISOBJ )
{
  RogueStringList* _auto_anchored_context_0_0 = 0;
  (void)_auto_anchored_context_0_0;
  RogueString* _auto_obj_1_1 = 0;
  (void)_auto_obj_1_1;
  RogueStringList* _auto_anchored_context_2_2 = 0;
  (void)_auto_anchored_context_2_2;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_0_0 );
    if (RogueStringList__is_empty((_auto_anchored_context_0_0=THISOBJ->available)))
    {
      {
        RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_obj_1_1 );
        _auto_obj_1_1 = ROGUE_CREATE_OBJECT( RogueString );
        RogueString__init(_auto_obj_1_1);
        TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
        return _auto_obj_1_1;
      }
    }
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_2_2 );
    RogueString* _auto_result_0 = (RogueString*)RogueStringList__remove_last((_auto_anchored_context_2_2=THISOBJ->available));
    TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
    return _auto_result_0;
  }
}

void RogueObjectPoolxRogueStringx__on_end_use__RogueString( RogueObjectPoolxRogueStringx* THISOBJ, RogueString* object_0 )
{
  RogueObject* poolable_1 = 0;
  (void)poolable_1;
  RogueStringList* _auto_anchored_context_0_2 = 0;
  (void)_auto_anchored_context_0_2;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &poolable_1 );
    {
      poolable_1 = ((RogueObject*)object_0);
      if (!!poolable_1)
      {
        {
          Rogue_dispatch_on_return_to_pool_(poolable_1);
        }
      }
    }
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_0_2 );
    RogueStringList__add__RogueString( (_auto_anchored_context_0_2=THISOBJ->available), object_0 );
  }
  TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
}

void RogueObjectPoolxRogueStringx__init_object( RogueObjectPoolxRogueStringx* THISOBJ )
{
  RogueStringList* _auto_obj_0_0 = 0;
  (void)_auto_obj_0_0;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    RogueObject__init_object(((RogueObject*)THISOBJ));
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_obj_0_0 );
    _auto_obj_0_0 = ROGUE_CREATE_OBJECT( RogueStringList );
    RogueStringList__init(_auto_obj_0_0);
    THISOBJ->available = _auto_obj_0_0;
  }
  TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
}

RogueString* RogueObjectPoolxRogueStringx__type_name( RogueObjectPoolxRogueStringx* THISOBJ )
{
  {
    RogueString* _auto_result_0 = (RogueString*)RogueObject____type_name__RogueInt32( 54 );
    return _auto_result_0;
  }
}

void RogueConsoleEventTypeList_gc_trace( void* THISOBJ )
{
  ((RogueObject*)THISOBJ)->__refcount = ~((RogueObject*)THISOBJ)->__refcount;
}

void RogueConsoleEventTypeList__init__RogueInt32( RogueConsoleEventTypeList* THISOBJ, RogueInt32 capacity_0 )
{
  {
    RogueConsoleEventTypeList__reserve__RogueInt32( THISOBJ, capacity_0 );
  }
}

void RogueConsoleEventTypeList__init_object( RogueConsoleEventTypeList* THISOBJ )
{
  {
    RogueObject__init_object(((RogueObject*)THISOBJ));
    {
      {
        THISOBJ->element_size = sizeof(THISOBJ->element_type);
      }
    }
  }
}

void RogueConsoleEventTypeList__on_cleanup( RogueConsoleEventTypeList* THISOBJ )
{
  {
    if (THISOBJ->as_bytes)
    {
      if (THISOBJ->is_borrowed) THISOBJ->is_borrowed = 0;
      else              ROGUE_FREE( THISOBJ->as_bytes );
      THISOBJ->as_bytes = 0;
      THISOBJ->capacity = 0;
      THISOBJ->count = 0;
    }
  }
}

void RogueConsoleEventTypeList__add__RogueConsoleEventType( RogueConsoleEventTypeList* THISOBJ, RogueConsoleEventType value_0 )
{
  {
    RogueConsoleEventTypeList__reserve__RogueInt32( THISOBJ, 1 );
    {
      {
        ((RogueConsoleEventType*)(THISOBJ->data))[THISOBJ->count++] = value_0;
      }
    }
  }
}

void RogueConsoleEventTypeList__clear( RogueConsoleEventTypeList* THISOBJ )
{
  {
    RogueConsoleEventTypeList__discard_from__RogueInt32( THISOBJ, 0 );
  }
}

RogueString* RogueConsoleEventTypeList__description( RogueConsoleEventTypeList* THISOBJ )
{
  RogueString* result_0 = 0;
  (void)result_0;
  RogueInt32 i_1 = 0;
  (void)i_1;
  RogueConsoleEventTypeList* _auto_collection_0_2 = 0;
  (void)_auto_collection_0_2;
  RogueInt32 _auto_count_1_3 = 0;
  (void)_auto_count_1_3;
  RogueConsoleEventType value_4 = {0};
  (void)value_4;
  RogueLogical _auto_condition_0_5 = 0;
  (void)_auto_condition_0_5;
  RogueString* _auto_anchored_arg_0_1_6 = 0;
  (void)_auto_anchored_arg_0_1_6;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &result_0 );
    result_0 = ROGUE_CREATE_OBJECT( RogueString );
    RogueString__init(result_0);
    RogueString__print__RogueCharacter( result_0, '[' );
    i_1 = 0;
    _auto_collection_0_2 = 0;
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_collection_0_2 );
    _auto_collection_0_2 = THISOBJ;
    _auto_count_1_3 = 0;
    _auto_count_1_3 = _auto_collection_0_2->count;
    value_4 = (RogueConsoleEventType){0};
    _auto_condition_0_5 = i_1 < _auto_count_1_3;
    goto _auto_loop_condition_0;
    do
    {
      {
        value_4 = RogueConsoleEventTypeList__get__RogueInt32( _auto_collection_0_2, i_1 );
        if (i_1 > 0)
        {
          {
            RogueString__print__RogueCharacter( result_0, ',' );
          }
        }
        _auto_anchored_arg_0_1_6 = 0;
        RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_arg_0_1_6 );
        RogueString__print__RogueString( result_0, (_auto_anchored_arg_0_1_6=RogueConsoleEventType__toxRogueStringx(value_4)) );
      }
      {
        ++i_1;
        _auto_condition_0_5 = i_1 < _auto_count_1_3;
      }
      _auto_loop_condition_0:;
    }
    while (_auto_condition_0_5);
    RogueString__print__RogueCharacter( result_0, ']' );
    TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
    return result_0;
  }
}

void RogueConsoleEventTypeList__discard_from__RogueInt32( RogueConsoleEventTypeList* THISOBJ, RogueInt32 index_0 )
{
  RogueInt32 n_1 = 0;
  (void)n_1;

  {
    n_1 = THISOBJ->count - index_0;
    if (n_1 > 0)
    {
      {
        RogueConsoleEventTypeList__zero__RogueInt32_RogueOptionalInt32( THISOBJ, index_0, (RogueOptionalInt32) {n_1,1} );
        THISOBJ->count = index_0;
      }
    }
  }
}

RogueConsoleEventType RogueConsoleEventTypeList__get__RogueInt32( RogueConsoleEventTypeList* THISOBJ, RogueInt32 index_0 )
{
  {
    {
      {
        RogueConsoleEventType _auto_result_0 = ((RogueConsoleEventType*)(THISOBJ->data))[index_0];
        return _auto_result_0;
      }
    }
  }
}

void RogueConsoleEventTypeList__on_return_to_pool( RogueConsoleEventTypeList* THISOBJ )
{
  {
    RogueConsoleEventTypeList__clear(THISOBJ);
  }
}

void RogueConsoleEventTypeList__reserve__RogueInt32( RogueConsoleEventTypeList* THISOBJ, RogueInt32 additional_capacity_0 )
{
  RogueInt32 required_capacity_1 = 0;
  (void)required_capacity_1;

  {
    required_capacity_1 = (THISOBJ->count + additional_capacity_0);
    if (required_capacity_1 <= THISOBJ->capacity)
    {
      {
        return;
      }
    }
    required_capacity_1 = RogueInt32__or_larger__RogueInt32( RogueInt32__or_larger__RogueInt32( required_capacity_1, THISOBJ->count * 2 ), 10 );
    int total_size = required_capacity_1 * THISOBJ->element_size;
    RogueMM_bytes_allocated_since_gc += total_size;
    RogueByte* new_data = (RogueByte*) ROGUE_MALLOC( total_size );

    if (THISOBJ->as_bytes)
    {
      int old_size = THISOBJ->capacity * THISOBJ->element_size;
      RogueByte* old_data = THISOBJ->as_bytes;
      memcpy( new_data, old_data, old_size );
      memset( new_data+old_size, 0, total_size-old_size );
      if (THISOBJ->is_borrowed) THISOBJ->is_borrowed = 0;
      else              ROGUE_FREE( old_data );
    }
    else
    {
      memset( new_data, 0, total_size );
    }
    THISOBJ->as_bytes = new_data;
    THISOBJ->capacity = required_capacity_1;
  }
}

RogueString* RogueConsoleEventTypeList__toxRogueStringx( RogueConsoleEventTypeList* THISOBJ )
{
  {
    RogueString* _auto_result_0 = (RogueString*)RogueConsoleEventTypeList__description(THISOBJ);
    return _auto_result_0;
  }
}

void RogueConsoleEventTypeList__zero__RogueInt32_RogueOptionalInt32( RogueConsoleEventTypeList* THISOBJ, RogueInt32 i1_0, RogueOptionalInt32 count_1 )
{
  RogueInt32 n_2 = 0;
  (void)n_2;

  {
    n_2 = (count_1.exists ? count_1.value : THISOBJ->count);
    memset( THISOBJ->as_bytes + i1_0*THISOBJ->element_size, 0, n_2*THISOBJ->element_size );
  }
}

RogueString* RogueConsoleEventTypeList__type_name( RogueConsoleEventTypeList* THISOBJ )
{
  {
    RogueString* _auto_result_0 = (RogueString*)RogueObject____type_name__RogueInt32( 56 );
    return _auto_result_0;
  }
}

void RogueConsoleErrorPrinter_gc_trace( void* THISOBJ )
{
  ((RogueObject*)THISOBJ)->__refcount = ~((RogueObject*)THISOBJ)->__refcount;

  RogueObject* ref;
  if ((ref = (RogueObject*)((RogueConsoleErrorPrinter*)THISOBJ)->output_buffer) && ref->__refcount >= 0) ref->__type->fn_gc_trace(ref);
}

void RogueConsoleErrorPrinter__flush__RogueString( RogueConsoleErrorPrinter* THISOBJ, RogueString* buffer_0 )
{
  {
    RogueConsoleErrorPrinter__write__RogueString( THISOBJ, buffer_0 );
    RogueString__clear(buffer_0);
  }
}

void RogueConsoleErrorPrinter__write__RogueString( RogueConsoleErrorPrinter* THISOBJ, RogueString* value_0 )
{
  {
    #if defined(ROGUE_PLATFORM_ANDROID)
      __android_log_print( ANDROID_LOG_ERROR, "Rogue", "%s", value_0->data->as_utf8 );
    #else
      Rogue_fwrite( (char*)value_0->data->as_utf8, value_0->data->count, STDERR_FILENO );
    #endif
  }
}

void RogueConsoleErrorPrinter__init_object( RogueConsoleErrorPrinter* THISOBJ )
{
  RogueString* _auto_obj_0_0 = 0;
  (void)_auto_obj_0_0;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    RogueObject__init_object(((RogueObject*)THISOBJ));
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_obj_0_0 );
    _auto_obj_0_0 = ROGUE_CREATE_OBJECT( RogueString );
    RogueString__init(_auto_obj_0_0);
    THISOBJ->output_buffer = _auto_obj_0_0;
  }
  TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
}

RogueString* RogueConsoleErrorPrinter__type_name( RogueConsoleErrorPrinter* THISOBJ )
{
  {
    RogueString* _auto_result_0 = (RogueString*)RogueObject____type_name__RogueInt32( 57 );
    return _auto_result_0;
  }
}

void RogueConsoleErrorPrinter__flush( RogueConsoleErrorPrinter* THISOBJ )
{
  RogueString* _auto_anchored_arg_0_0_0 = 0;
  (void)_auto_anchored_arg_0_0_0;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    _auto_anchored_arg_0_0_0 = 0;
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_arg_0_0_0 );
    RogueConsoleErrorPrinter__flush__RogueString( THISOBJ, (_auto_anchored_arg_0_0_0=THISOBJ->output_buffer) );
  }
  TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
}

void RogueConsoleErrorPrinter__print__RogueCharacter( RogueConsoleErrorPrinter* THISOBJ, RogueCharacter value_0 )
{
  RogueString* _auto_anchored_context_0_1 = 0;
  (void)_auto_anchored_context_0_1;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_0_1 );
    RogueString__print__RogueCharacter( (_auto_anchored_context_0_1=THISOBJ->output_buffer), value_0 );
    if (value_0 == ((RogueCharacter)10))
    {
      {
        RogueConsoleErrorPrinter__flush(THISOBJ);
      }
    }
  }
  TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
}

void RogueConsoleErrorPrinter__print__RogueObject( RogueConsoleErrorPrinter* THISOBJ, RogueObject* value_0 )
{
  RogueString* _auto_anchored_context_0_1 = 0;
  (void)_auto_anchored_context_0_1;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_0_1 );
    RogueString__print__RogueObject( (_auto_anchored_context_0_1=THISOBJ->output_buffer), value_0 );
  }
  TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
}

void RogueConsoleErrorPrinter__print__RogueString( RogueConsoleErrorPrinter* THISOBJ, RogueString* value_0 )
{
  RogueString* _auto_anchored_context_0_1 = 0;
  (void)_auto_anchored_context_0_1;
  RogueString* _auto_anchored_context_1_2 = 0;
  (void)_auto_anchored_context_1_2;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_0_1 );
    RogueString__print__RogueString( (_auto_anchored_context_0_1=THISOBJ->output_buffer), value_0 );
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_1_2 );
    if (RogueString__count((_auto_anchored_context_1_2=THISOBJ->output_buffer)) > 1024)
    {
      {
        RogueConsoleErrorPrinter__flush(THISOBJ);
      }
    }
  }
  TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
}

void RogueConsoleErrorPrinter__println( RogueConsoleErrorPrinter* THISOBJ )
{
  RogueString* _auto_anchored_context_0_0 = 0;
  (void)_auto_anchored_context_0_0;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_0_0 );
    RogueString__print__RogueCharacter( (_auto_anchored_context_0_0=THISOBJ->output_buffer), ((RogueCharacter)10) );
    RogueConsoleErrorPrinter__flush(THISOBJ);
  }
  TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
}

void RogueConsoleErrorPrinter__println__RogueObject( RogueConsoleErrorPrinter* THISOBJ, RogueObject* value_0 )
{
  {
    RogueConsoleErrorPrinter__print__RogueObject( THISOBJ, value_0 );
    RogueConsoleErrorPrinter__println(THISOBJ);
  }
}

void RogueConsoleErrorPrinter__println__RogueString( RogueConsoleErrorPrinter* THISOBJ, RogueString* value_0 )
{
  {
    RogueConsoleErrorPrinter__print__RogueString( THISOBJ, value_0 );
    RogueConsoleErrorPrinter__println(THISOBJ);
  }
}

void RogueFunction_260_gc_trace( void* THISOBJ )
{
  ((RogueObject*)THISOBJ)->__refcount = ~((RogueObject*)THISOBJ)->__refcount;
}

void RogueFunction_260__call( RogueFunction_260* THISOBJ )
{
  {
    RogueConsole__set_immediate_mode__RogueLogical( ROGUE_SINGLETON(RogueConsole), 0 );
    RogueConsoleCursor__show__RoguePrintWriter( ROGUE_SINGLETON(RogueConsole)->cursor, ((RogueObject*)ROGUE_SINGLETON(RogueGlobal)) );
  }
}

void RogueFunction_260__init_object( RogueFunction_260* THISOBJ )
{
  {
    RogueOPARENFunctionOPARENCPARENCPAREN__init_object(((RogueOPARENFunctionOPARENCPARENCPAREN*)THISOBJ));
  }
}

RogueString* RogueFunction_260__type_name( RogueFunction_260* THISOBJ )
{
  {
    RogueString* _auto_result_0 = (RogueString*)RogueObject____type_name__RogueInt32( 58 );
    return _auto_result_0;
  }
}

void RogueStandardConsoleMode_gc_trace( void* THISOBJ )
{
  ((RogueObject*)THISOBJ)->__refcount = ~((RogueObject*)THISOBJ)->__refcount;
}

void RogueStandardConsoleMode__init( RogueStandardConsoleMode* THISOBJ )
{
  RogueConsole* console_0 = 0;
  (void)console_0;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;
  (void)_auto_local_pointer_fp_0;

  {
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &console_0 );
    console_0 = ROGUE_SINGLETON(RogueConsole);
    #if defined(ROGUE_PLATFORM_WINDOWS)
      HANDLE h_stdin = GetStdHandle( STD_INPUT_HANDLE );
      DWORD mode;
      GetConsoleMode( h_stdin, &mode );
      if (console_0->windows_in_quick_edit_mode) mode |= ENABLE_QUICK_EDIT_MODE;
      mode |= (ENABLE_EXTENDED_FLAGS | ENABLE_MOUSE_INPUT);
      SetConsoleMode( h_stdin, mode );
    #else
      tcsetattr( STDIN_FILENO, TCSANOW, &console_0->original_terminal_settings );
      fcntl( STDIN_FILENO, F_SETFL, console_0->original_stdin_flags );
    #endif
  }
  TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
}

RogueLogical RogueStandardConsoleMode__has_another( RogueStandardConsoleMode* THISOBJ )
{
  RogueConsole* console_0 = 0;
  (void)console_0;
  RogueByte b1_1 = 0;
  (void)b1_1;
  RogueInt32 result_2 = 0;
  (void)result_2;
  RogueByteList* _auto_anchored_context_0_3 = 0;
  (void)_auto_anchored_context_0_3;
  RogueByteList* _auto_anchored_context_1_4 = 0;
  (void)_auto_anchored_context_1_4;
  RogueByteList* _auto_anchored_context_2_5 = 0;
  (void)_auto_anchored_context_2_5;
  RogueByteList* _auto_anchored_context_3_6 = 0;
  (void)_auto_anchored_context_3_6;
  RogueByteList* _auto_anchored_context_4_7 = 0;
  (void)_auto_anchored_context_4_7;
  RogueByteList* _auto_anchored_context_5_8 = 0;
  (void)_auto_anchored_context_5_8;
  RogueByteList* _auto_anchored_context_6_9 = 0;
  (void)_auto_anchored_context_6_9;
  RogueByteList* _auto_anchored_context_7_10 = 0;
  (void)_auto_anchored_context_7_10;
  RogueByteList* _auto_anchored_context_8_11 = 0;
  (void)_auto_anchored_context_8_11;
  RogueByteList* _auto_anchored_context_9_12 = 0;
  (void)_auto_anchored_context_9_12;
  RogueByteList* _auto_anchored_context_10_13 = 0;
  (void)_auto_anchored_context_10_13;
  RogueByteList* _auto_anchored_context_11_14 = 0;
  (void)_auto_anchored_context_11_14;
  RogueByteList* _auto_anchored_context_12_15 = 0;
  (void)_auto_anchored_context_12_15;
  RogueByteList* _auto_anchored_context_13_16 = 0;
  (void)_auto_anchored_context_13_16;
  RogueByteList* _auto_anchored_context_14_17 = 0;
  (void)_auto_anchored_context_14_17;
  RogueByteList* _auto_anchored_context_15_18 = 0;
  (void)_auto_anchored_context_15_18;
  RogueByteList* _auto_anchored_context_16_19 = 0;
  (void)_auto_anchored_context_16_19;
  RogueByteList* _auto_anchored_context_17_20 = 0;
  (void)_auto_anchored_context_17_20;
  RogueByteList* _auto_anchored_context_18_21 = 0;
  (void)_auto_anchored_context_18_21;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    if (!THISOBJ->next_input_character.exists)
    {
      {
        RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &console_0 );
        console_0 = ROGUE_SINGLETON(RogueConsole);
        RogueConsole___fill_input_buffer__RogueInt32( console_0, 1 );
        if (!!console_0->_input_bytes->count)
        {
          {
            RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_0_3 );
            b1_1 = RogueByteList__remove_first((_auto_anchored_context_0_3=console_0->_input_bytes));
            {
              {
                if (!(console_0->decode_utf8)) goto _auto_unsatisfied_0;
                if (!(!!console_0->_input_bytes->count)) goto _auto_unsatisfied_0;
                if (b1_1 == 27)
                {
                  {
                    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_1_4 );
                    if (!((console_0->_input_bytes->count >= 2) && (RogueByteList__first((_auto_anchored_context_1_4=console_0->_input_bytes)) == 91))) goto _auto_unsatisfied_0;
                    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_2_5 );
                    RogueByteList__remove_first((_auto_anchored_context_2_5=console_0->_input_bytes));
                    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_3_6 );
                    THISOBJ->next_input_character = (RogueOptionalInt32) {((((RogueInt32)RogueByteList__remove_first((_auto_anchored_context_3_6=console_0->_input_bytes))) - 65) + 17),1};
                  }
                }
                else
                {
                  {
                    {
                      {
                        if (!(((RogueInt32)b1_1) >= 192)) goto _auto_unsatisfied_0;
                        result_2 = 0;
                        if ((((RogueInt32)b1_1) & 224) == 192)
                        {
                          {
                            if (!(console_0->_input_bytes->count >= 1)) goto _auto_unsatisfied_0;
                            result_2 = ((RogueInt32)b1_1) & 31;
                            RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_4_7 );
                            result_2 = (result_2 << 6) | (((RogueInt32)RogueByteList__remove_first((_auto_anchored_context_4_7=console_0->_input_bytes))) & 63);
                          }
                        }
                        else
                        {
                          {
                            if ((((RogueInt32)b1_1) & 240) == 224)
                            {
                              {
                                if (!(console_0->_input_bytes->count >= 2)) goto _auto_unsatisfied_0;
                                result_2 = ((RogueInt32)b1_1) & 15;
                                RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_5_8 );
                                result_2 = (result_2 << 6) | (((RogueInt32)RogueByteList__remove_first((_auto_anchored_context_5_8=console_0->_input_bytes))) & 63);
                                RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_6_9 );
                                result_2 = (result_2 << 6) | (((RogueInt32)RogueByteList__remove_first((_auto_anchored_context_6_9=console_0->_input_bytes))) & 63);
                              }
                            }
                            else
                            {
                              {
                                if ((((RogueInt32)b1_1) & 248) == 240)
                                {
                                  {
                                    if (!(console_0->_input_bytes->count >= 3)) goto _auto_unsatisfied_0;
                                    result_2 = ((RogueInt32)b1_1) & 7;
                                    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_7_10 );
                                    result_2 = (result_2 << 6) | (((RogueInt32)RogueByteList__remove_first((_auto_anchored_context_7_10=console_0->_input_bytes))) & 63);
                                    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_8_11 );
                                    result_2 = (result_2 << 6) | (((RogueInt32)RogueByteList__remove_first((_auto_anchored_context_8_11=console_0->_input_bytes))) & 63);
                                    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_9_12 );
                                    result_2 = (result_2 << 6) | (((RogueInt32)RogueByteList__remove_first((_auto_anchored_context_9_12=console_0->_input_bytes))) & 63);
                                  }
                                }
                                else
                                {
                                  {
                                    if ((((RogueInt32)b1_1) & 252) == 248)
                                    {
                                      {
                                        if (!(console_0->_input_bytes->count >= 4)) goto _auto_unsatisfied_0;
                                        result_2 = ((RogueInt32)b1_1) & 3;
                                        RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_10_13 );
                                        result_2 = (result_2 << 6) | (((RogueInt32)RogueByteList__remove_first((_auto_anchored_context_10_13=console_0->_input_bytes))) & 63);
                                        RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_11_14 );
                                        result_2 = (result_2 << 6) | (((RogueInt32)RogueByteList__remove_first((_auto_anchored_context_11_14=console_0->_input_bytes))) & 63);
                                        RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_12_15 );
                                        result_2 = (result_2 << 6) | (((RogueInt32)RogueByteList__remove_first((_auto_anchored_context_12_15=console_0->_input_bytes))) & 63);
                                        RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_13_16 );
                                        result_2 = (result_2 << 6) | (((RogueInt32)RogueByteList__remove_first((_auto_anchored_context_13_16=console_0->_input_bytes))) & 63);
                                      }
                                    }
                                    else
                                    {
                                      {
                                        {
                                          {
                                            if (!(console_0->_input_bytes->count >= 5)) goto _auto_unsatisfied_0;
                                            result_2 = ((RogueInt32)b1_1) & 1;
                                            RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_14_17 );
                                            result_2 = (result_2 << 6) | (((RogueInt32)RogueByteList__remove_first((_auto_anchored_context_14_17=console_0->_input_bytes))) & 63);
                                            RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_15_18 );
                                            result_2 = (result_2 << 6) | (((RogueInt32)RogueByteList__remove_first((_auto_anchored_context_15_18=console_0->_input_bytes))) & 63);
                                            RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_16_19 );
                                            result_2 = (result_2 << 6) | (((RogueInt32)RogueByteList__remove_first((_auto_anchored_context_16_19=console_0->_input_bytes))) & 63);
                                            RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_17_20 );
                                            result_2 = (result_2 << 6) | (((RogueInt32)RogueByteList__remove_first((_auto_anchored_context_17_20=console_0->_input_bytes))) & 63);
                                            RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_18_21 );
                                            result_2 = (result_2 << 6) | (((RogueInt32)RogueByteList__remove_first((_auto_anchored_context_18_21=console_0->_input_bytes))) & 63);
                                          }
                                        }
                                      }
                                    }
                                  }
                                }
                              }
                            }
                          }
                        }
                        THISOBJ->next_input_character = (RogueOptionalInt32) {result_2,1};
                      }
                    }
                  }
                }
                goto _auto_escape_1;
              }
              _auto_unsatisfied_0:;
              {
                THISOBJ->next_input_character = (RogueOptionalInt32) {((RogueInt32)b1_1),1};
              }
            }
            _auto_escape_1:;
            if ((console_0->_input_bytes->count > 0) && (console_0->_input_bytes->count < 6))
            {
              {
                RogueConsole___fill_input_buffer__RogueInt32( console_0, 1 );
              }
            }
          }
        }
      }
    }
    RogueLogical _auto_result_2 = THISOBJ->next_input_character.exists;
    TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
    return _auto_result_2;
  }
}

RogueCharacter RogueStandardConsoleMode__read( RogueStandardConsoleMode* THISOBJ )
{
  RogueInt32 result_0 = 0;
  (void)result_0;

  {
    if (!RogueStandardConsoleMode__has_another(THISOBJ))
    {
      {
        RogueCharacter _auto_result_0 = ((RogueCharacter)0);
        return _auto_result_0;
      }
    }
    result_0 = THISOBJ->next_input_character.value;
    THISOBJ->next_input_character = (RogueOptionalInt32){0};
    RogueCharacter _auto_result_1 = ((RogueCharacter)result_0);
    return _auto_result_1;
  }
}

void RogueStandardConsoleMode__init_object( RogueStandardConsoleMode* THISOBJ )
{
  {
    RogueConsoleMode__init_object(((RogueConsoleMode*)THISOBJ));
  }
}

RogueString* RogueStandardConsoleMode__type_name( RogueStandardConsoleMode* THISOBJ )
{
  {
    RogueString* _auto_result_0 = (RogueString*)RogueObject____type_name__RogueInt32( 59 );
    return _auto_result_0;
  }
}

void RogueFunction_262_gc_trace( void* THISOBJ )
{
  ((RogueObject*)THISOBJ)->__refcount = ~((RogueObject*)THISOBJ)->__refcount;
}

void RogueFunction_262__call( RogueFunction_262* THISOBJ )
{
  {
    RogueGlobal__flush(ROGUE_SINGLETON(RogueGlobal));
  }
}

void RogueFunction_262__init_object( RogueFunction_262* THISOBJ )
{
  {
    RogueOPARENFunctionOPARENCPARENCPAREN__init_object(((RogueOPARENFunctionOPARENCPARENCPAREN*)THISOBJ));
  }
}

RogueString* RogueFunction_262__type_name( RogueFunction_262* THISOBJ )
{
  {
    RogueString* _auto_result_0 = (RogueString*)RogueObject____type_name__RogueInt32( 60 );
    return _auto_result_0;
  }
}

void RogueConsoleEventList_gc_trace( void* THISOBJ )
{
  ((RogueObject*)THISOBJ)->__refcount = ~((RogueObject*)THISOBJ)->__refcount;
}

void RogueConsoleEventList__init( RogueConsoleEventList* THISOBJ )
{
}

void RogueConsoleEventList__init_object( RogueConsoleEventList* THISOBJ )
{
  {
    RogueObject__init_object(((RogueObject*)THISOBJ));
    {
      {
        THISOBJ->element_size = sizeof(THISOBJ->element_type);
      }
    }
  }
}

void RogueConsoleEventList__on_cleanup( RogueConsoleEventList* THISOBJ )
{
  {
    if (THISOBJ->as_bytes)
    {
      if (THISOBJ->is_borrowed) THISOBJ->is_borrowed = 0;
      else              ROGUE_FREE( THISOBJ->as_bytes );
      THISOBJ->as_bytes = 0;
      THISOBJ->capacity = 0;
      THISOBJ->count = 0;
    }
  }
}

void RogueConsoleEventList__add__RogueConsoleEvent( RogueConsoleEventList* THISOBJ, RogueConsoleEvent value_0 )
{
  {
    RogueConsoleEventList__reserve__RogueInt32( THISOBJ, 1 );
    {
      {
        ((RogueConsoleEvent*)(THISOBJ->data))[THISOBJ->count++] = value_0;
      }
    }
  }
}

void RogueConsoleEventList__clear( RogueConsoleEventList* THISOBJ )
{
  {
    RogueConsoleEventList__discard_from__RogueInt32( THISOBJ, 0 );
  }
}

void RogueConsoleEventList__copy__RogueInt32_RogueInt32_RogueConsoleEventList_RogueInt32( RogueConsoleEventList* THISOBJ, RogueInt32 src_i1_0, RogueInt32 src_count_1, RogueConsoleEventList* dest_2, RogueInt32 dest_i1_3 )
{
  {
    if (src_count_1 <= 0)
    {
      {
        return;
      }
    }
    RogueConsoleEventList__expand_to_count__RogueInt32( dest_2, (dest_i1_3 + src_count_1) );
    if (THISOBJ == dest_2)
    {
      {
        memmove(
          dest_2->as_bytes + dest_i1_3*THISOBJ->element_size,
          THISOBJ->as_bytes + src_i1_0*THISOBJ->element_size,
          src_count_1 * THISOBJ->element_size
        );
      }
    }
    else
    {
      {
        {
          {
            memcpy(
              dest_2->as_bytes + dest_i1_3*THISOBJ->element_size,
              THISOBJ->as_bytes + src_i1_0*THISOBJ->element_size,
              src_count_1 * THISOBJ->element_size
            );
          }
        }
      }
    }
  }
}

RogueString* RogueConsoleEventList__description( RogueConsoleEventList* THISOBJ )
{
  RogueString* result_0 = 0;
  (void)result_0;
  RogueInt32 i_1 = 0;
  (void)i_1;
  RogueConsoleEventList* _auto_collection_0_2 = 0;
  (void)_auto_collection_0_2;
  RogueInt32 _auto_count_1_3 = 0;
  (void)_auto_count_1_3;
  RogueConsoleEvent value_4 = {{0}};
  (void)value_4;
  RogueLogical _auto_condition_0_5 = 0;
  (void)_auto_condition_0_5;
  RogueString* _auto_anchored_arg_0_1_6 = 0;
  (void)_auto_anchored_arg_0_1_6;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &result_0 );
    result_0 = ROGUE_CREATE_OBJECT( RogueString );
    RogueString__init(result_0);
    RogueString__print__RogueCharacter( result_0, '[' );
    i_1 = 0;
    _auto_collection_0_2 = 0;
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_collection_0_2 );
    _auto_collection_0_2 = THISOBJ;
    _auto_count_1_3 = 0;
    _auto_count_1_3 = _auto_collection_0_2->count;
    value_4 = (RogueConsoleEvent){{0}};
    _auto_condition_0_5 = i_1 < _auto_count_1_3;
    goto _auto_loop_condition_0;
    do
    {
      {
        value_4 = RogueConsoleEventList__get__RogueInt32( _auto_collection_0_2, i_1 );
        if (i_1 > 0)
        {
          {
            RogueString__print__RogueCharacter( result_0, ',' );
          }
        }
        _auto_anchored_arg_0_1_6 = 0;
        RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_arg_0_1_6 );
        RogueString__print__RogueString( result_0, (_auto_anchored_arg_0_1_6=RogueConsoleEvent__toxRogueStringx(value_4)) );
      }
      {
        ++i_1;
        _auto_condition_0_5 = i_1 < _auto_count_1_3;
      }
      _auto_loop_condition_0:;
    }
    while (_auto_condition_0_5);
    RogueString__print__RogueCharacter( result_0, ']' );
    TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
    return result_0;
  }
}

void RogueConsoleEventList__discard_from__RogueInt32( RogueConsoleEventList* THISOBJ, RogueInt32 index_0 )
{
  RogueInt32 n_1 = 0;
  (void)n_1;

  {
    n_1 = THISOBJ->count - index_0;
    if (n_1 > 0)
    {
      {
        RogueConsoleEventList__zero__RogueInt32_RogueOptionalInt32( THISOBJ, index_0, (RogueOptionalInt32) {n_1,1} );
        THISOBJ->count = index_0;
      }
    }
  }
}

void RogueConsoleEventList__ensure_capacity__RogueInt32( RogueConsoleEventList* THISOBJ, RogueInt32 desired_capacity_0 )
{
  {
    RogueConsoleEventList__reserve__RogueInt32( THISOBJ, desired_capacity_0 - THISOBJ->count );
  }
}

void RogueConsoleEventList__expand_to_count__RogueInt32( RogueConsoleEventList* THISOBJ, RogueInt32 minimum_count_0 )
{
  {
    if (THISOBJ->count < minimum_count_0)
    {
      {
        RogueConsoleEventList__ensure_capacity__RogueInt32( THISOBJ, minimum_count_0 );
        THISOBJ->count = minimum_count_0;
      }
    }
  }
}

RogueConsoleEvent RogueConsoleEventList__get__RogueInt32( RogueConsoleEventList* THISOBJ, RogueInt32 index_0 )
{
  {
    {
      {
        RogueConsoleEvent _auto_result_0 = ((RogueConsoleEvent*)(THISOBJ->data))[index_0];
        return _auto_result_0;
      }
    }
  }
}

void RogueConsoleEventList__on_return_to_pool( RogueConsoleEventList* THISOBJ )
{
  {
    RogueConsoleEventList__clear(THISOBJ);
  }
}

RogueConsoleEvent RogueConsoleEventList__remove_first( RogueConsoleEventList* THISOBJ )
{
  RogueConsoleEvent result_0 = {{0}};
  (void)result_0;

  {
    result_0 = RogueConsoleEventList__get__RogueInt32( THISOBJ, 0 );
    RogueConsoleEventList__shift__RogueInt32( THISOBJ, -1 );
    return result_0;
  }
}

void RogueConsoleEventList__reserve__RogueInt32( RogueConsoleEventList* THISOBJ, RogueInt32 additional_capacity_0 )
{
  RogueInt32 required_capacity_1 = 0;
  (void)required_capacity_1;

  {
    required_capacity_1 = (THISOBJ->count + additional_capacity_0);
    if (required_capacity_1 <= THISOBJ->capacity)
    {
      {
        return;
      }
    }
    required_capacity_1 = RogueInt32__or_larger__RogueInt32( RogueInt32__or_larger__RogueInt32( required_capacity_1, THISOBJ->count * 2 ), 10 );
    int total_size = required_capacity_1 * THISOBJ->element_size;
    RogueMM_bytes_allocated_since_gc += total_size;
    RogueByte* new_data = (RogueByte*) ROGUE_MALLOC( total_size );

    if (THISOBJ->as_bytes)
    {
      int old_size = THISOBJ->capacity * THISOBJ->element_size;
      RogueByte* old_data = THISOBJ->as_bytes;
      memcpy( new_data, old_data, old_size );
      memset( new_data+old_size, 0, total_size-old_size );
      if (THISOBJ->is_borrowed) THISOBJ->is_borrowed = 0;
      else              ROGUE_FREE( old_data );
    }
    else
    {
      memset( new_data, 0, total_size );
    }
    THISOBJ->as_bytes = new_data;
    THISOBJ->capacity = required_capacity_1;
  }
}

void RogueConsoleEventList__shift__RogueInt32( RogueConsoleEventList* THISOBJ, RogueInt32 delta_0 )
{
  {
    if (delta_0 == 0)
    {
      {
        return;
      }
    }
    if (delta_0 > 0)
    {
      {
        RogueConsoleEventList__reserve__RogueInt32( THISOBJ, delta_0 );
        RogueConsoleEventList__copy__RogueInt32_RogueInt32_RogueConsoleEventList_RogueInt32( THISOBJ, 0, THISOBJ->count, THISOBJ, delta_0 );
        RogueConsoleEventList__zero__RogueInt32_RogueOptionalInt32( THISOBJ, 0, (RogueOptionalInt32) {delta_0,1} );
        THISOBJ->count += delta_0;
      }
    }
    else
    {
      {
        if ((-delta_0) >= THISOBJ->count)
        {
          {
            RogueConsoleEventList__clear(THISOBJ);
          }
        }
        else
        {
          {
            {
              {
                RogueConsoleEventList__copy__RogueInt32_RogueInt32_RogueConsoleEventList_RogueInt32( THISOBJ, -delta_0, (THISOBJ->count + delta_0), THISOBJ, 0 );
                RogueConsoleEventList__discard_from__RogueInt32( THISOBJ, (THISOBJ->count + delta_0) );
              }
            }
          }
        }
      }
    }
  }
}

RogueString* RogueConsoleEventList__toxRogueStringx( RogueConsoleEventList* THISOBJ )
{
  {
    RogueString* _auto_result_0 = (RogueString*)RogueConsoleEventList__description(THISOBJ);
    return _auto_result_0;
  }
}

void RogueConsoleEventList__zero__RogueInt32_RogueOptionalInt32( RogueConsoleEventList* THISOBJ, RogueInt32 i1_0, RogueOptionalInt32 count_1 )
{
  RogueInt32 n_2 = 0;
  (void)n_2;

  {
    n_2 = (count_1.exists ? count_1.value : THISOBJ->count);
    memset( THISOBJ->as_bytes + i1_0*THISOBJ->element_size, 0, n_2*THISOBJ->element_size );
  }
}

RogueString* RogueConsoleEventList__type_name( RogueConsoleEventList* THISOBJ )
{
  {
    RogueString* _auto_result_0 = (RogueString*)RogueObject____type_name__RogueInt32( 61 );
    return _auto_result_0;
  }
}

void RogueImmediateConsoleMode_gc_trace( void* THISOBJ )
{
  ((RogueObject*)THISOBJ)->__refcount = ~((RogueObject*)THISOBJ)->__refcount;

  RogueObject* ref;
  if ((ref = (RogueObject*)((RogueImmediateConsoleMode*)THISOBJ)->events) && ref->__refcount >= 0) ref->__type->fn_gc_trace(ref);
}

void RogueImmediateConsoleMode__init( RogueImmediateConsoleMode* THISOBJ )
{
  RogueGlobal* _auto_context_block_0_0 = 0;
  (void)_auto_context_block_0_0;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;
  (void)_auto_local_pointer_fp_0;

  {
    THISOBJ->windows_button_state = 0;
    #if defined(ROGUE_PLATFORM_WINDOWS)
      HANDLE h_stdin = GetStdHandle( STD_INPUT_HANDLE );
      DWORD mode;
      GetConsoleMode( h_stdin, &mode );
      SetConsoleMode( h_stdin, (mode & ~(ENABLE_QUICK_EDIT_MODE)) | ENABLE_EXTENDED_FLAGS | ENABLE_MOUSE_INPUT );
    #elif !defined(ROGUE_PLATFORM_EMBEDDED)
      struct termios settings;
      tcgetattr( STDIN_FILENO, &settings );
      settings.c_lflag &= ~(ECHO | ECHOE | ICANON);
      settings.c_cc[VMIN] = 0;
      settings.c_cc[VTIME] = 0;
      tcsetattr( STDIN_FILENO, TCSANOW, &settings );
      fcntl( STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO,F_GETFL)|O_NONBLOCK );
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_context_block_0_0 );
    _auto_context_block_0_0 = ROGUE_SINGLETON(RogueGlobal);
    RogueGlobal__flush(_auto_context_block_0_0);
    RogueGlobal__print__RogueString( _auto_context_block_0_0, str____1003h );
    RogueGlobal__flush(_auto_context_block_0_0);
    #endif
  }
  TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
}

void RogueImmediateConsoleMode___on_exit( RogueImmediateConsoleMode* THISOBJ )
{
  RogueGlobal* _auto_context_block_0_0 = 0;
  (void)_auto_context_block_0_0;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_context_block_0_0 );
    _auto_context_block_0_0 = ROGUE_SINGLETON(RogueGlobal);
    RogueGlobal__flush(_auto_context_block_0_0);
    RogueGlobal__print__RogueString( _auto_context_block_0_0, str____1003l );
    RogueGlobal__flush(_auto_context_block_0_0);
  }
  TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
}

RogueLogical RogueImmediateConsoleMode__has_another( RogueImmediateConsoleMode* THISOBJ )
{
  RogueInt32 _auto_index_0_0 = 0;
  (void)_auto_index_0_0;
  RogueConsoleEventList* _auto_collection_1_1 = 0;
  (void)_auto_collection_1_1;
  RogueInt32 _auto_count_2_2 = 0;
  (void)_auto_count_2_2;
  RogueConsoleEvent _auto_iterator_0_3 = {{0}};
  (void)_auto_iterator_0_3;
  RogueInt32 _auto_index_4_4 = 0;
  (void)_auto_index_4_4;
  RogueConsoleEventList* _auto_collection_5_5 = 0;
  (void)_auto_collection_5_5;
  RogueInt32 _auto_count_6_6 = 0;
  (void)_auto_count_6_6;
  RogueConsoleEvent _auto_iterator_1_7 = {{0}};
  (void)_auto_iterator_1_7;
  RogueLogical _auto_condition_0_8 = 0;
  (void)_auto_condition_0_8;
  RogueLogical _auto_condition_1_9 = 0;
  (void)_auto_condition_1_9;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    _auto_index_0_0 = 0;
    _auto_collection_1_1 = 0;
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_collection_1_1 );
    _auto_collection_1_1 = THISOBJ->events;
    _auto_count_2_2 = 0;
    _auto_count_2_2 = _auto_collection_1_1->count;
    _auto_iterator_0_3 = (RogueConsoleEvent){{0}};
    _auto_condition_0_8 = _auto_index_0_0 < _auto_count_2_2;
    goto _auto_loop_condition_0;
    do
    {
      {
        _auto_iterator_0_3 = RogueConsoleEventList__get__RogueInt32( _auto_collection_1_1, _auto_index_0_0 );
        if (RogueConsoleEvent__is_character(_auto_iterator_0_3))
        {
          {
            TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
            return 1;
          }
        }
      }
      {
        ++_auto_index_0_0;
        _auto_condition_0_8 = _auto_index_0_0 < _auto_count_2_2;
      }
      _auto_loop_condition_0:;
    }
    while (_auto_condition_0_8);
    RogueImmediateConsoleMode___fill_event_queue(THISOBJ);
    _auto_index_4_4 = 0;
    _auto_collection_5_5 = 0;
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_collection_5_5 );
    _auto_collection_5_5 = THISOBJ->events;
    _auto_count_6_6 = 0;
    _auto_count_6_6 = _auto_collection_5_5->count;
    _auto_iterator_1_7 = (RogueConsoleEvent){{0}};
    _auto_condition_1_9 = _auto_index_4_4 < _auto_count_6_6;
    goto _auto_loop_condition_1;
    do
    {
      {
        _auto_iterator_1_7 = RogueConsoleEventList__get__RogueInt32( _auto_collection_5_5, _auto_index_4_4 );
        if (RogueConsoleEvent__is_character(_auto_iterator_1_7))
        {
          {
            TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
            return 1;
          }
        }
      }
      {
        ++_auto_index_4_4;
        _auto_condition_1_9 = _auto_index_4_4 < _auto_count_6_6;
      }
      _auto_loop_condition_1:;
    }
    while (_auto_condition_1_9);
    TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
    return 0;
  }
}

RogueCharacter RogueImmediateConsoleMode__read( RogueImmediateConsoleMode* THISOBJ )
{
  RogueConsoleEvent event_0 = {{0}};
  (void)event_0;
  RogueLogical _auto_condition_0_1 = 0;
  (void)_auto_condition_0_1;
  RogueConsoleEventList* _auto_anchored_context_1_2 = 0;
  (void)_auto_anchored_context_1_2;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    if (!RogueImmediateConsoleMode__has_another(THISOBJ))
    {
      {
        RogueCharacter _auto_result_0 = ((RogueCharacter)0);
        TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
        return _auto_result_0;
      }
    }
    _auto_condition_0_1 = !!THISOBJ->events->count;
    goto _auto_loop_condition_1;
    do
    {
      {
        RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_1_2 );
        event_0 = RogueConsoleEventList__remove_first((_auto_anchored_context_1_2=THISOBJ->events));
        if (RogueConsoleEvent__is_character(event_0))
        {
          {
            RogueCharacter _auto_result_2 = ((RogueCharacter)event_0.x);
            TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
            return _auto_result_2;
          }
        }
      }
      {
        _auto_condition_0_1 = !!THISOBJ->events->count;
      }
      _auto_loop_condition_1:;
    }
    while (_auto_condition_0_1);
    RogueCharacter _auto_result_3 = ((RogueCharacter)0);
    TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
    return _auto_result_3;
  }
}

void RogueImmediateConsoleMode___fill_event_queue( RogueImmediateConsoleMode* THISOBJ )
{
  {
    if (RogueSystem__is_windows())
    {
      {
        RogueImmediateConsoleMode___fill_event_queue_windows(THISOBJ);
      }
    }
    else
    {
      {
        {
          {
            RogueImmediateConsoleMode___fill_event_queue_unix(THISOBJ);
          }
        }
      }
    }
  }
}

void RogueImmediateConsoleMode___fill_event_queue_windows( RogueImmediateConsoleMode* THISOBJ )
{
  RogueLogical force_input_blocking_0 = 0;
  (void)force_input_blocking_0;
  RogueRangeUpToLessThanIteratorxRogueInt32x _auto_collection_1_1 = {0};
  (void)_auto_collection_1_1;
  RogueOptionalInt32 _auto_next_2_2 = {0};
  (void)_auto_next_2_2;
  RogueInt32 i_3 = 0;
  (void)i_3;
  RogueWindowsInputRecord record_4 = {0};
  (void)record_4;
  RogueInt32 _auto_i_5_5 = 0;
  (void)_auto_i_5_5;
  RogueLogical _auto_condition_0_6 = 0;
  (void)_auto_condition_0_6;
  RogueLogical _auto_condition_1_7 = 0;
  (void)_auto_condition_1_7;
  RogueLogical _auto_condition_2_8 = 0;
  (void)_auto_condition_2_8;
  RogueLogical _auto_condition_3_9 = 0;
  (void)_auto_condition_3_9;

  {
    #if defined(ROGUE_PLATFORM_WINDOWS)
    force_input_blocking_0 = ROGUE_SINGLETON(RogueConsole)->force_input_blocking;
    HANDLE h_stdin = GetStdHandle( STD_INPUT_HANDLE );
    DWORD  event_count = 0;
    INPUT_RECORD buffer[100];

    if (force_input_blocking_0)
    {
      ReadConsoleInput( h_stdin, buffer, 100, &event_count );
    _auto_condition_0_6 = !!(RogueInt32)event_count;
    goto _auto_loop_condition_0;
    do
    {
      {
        _auto_collection_1_1 = (RogueRangeUpToLessThanIteratorxRogueInt32x){0};
        _auto_collection_1_1 = RogueRangeUpToLessThanxRogueInt32x__iterator((RogueRangeUpToLessThanxRogueInt32x) {0,(RogueInt32)event_count,1});
        _auto_next_2_2 = (RogueOptionalInt32){0};
        _auto_next_2_2 = RogueRangeUpToLessThanIteratorxRogueInt32x__read_another(&_auto_collection_1_1);
        i_3 = 0;
        _auto_condition_1_7 = _auto_next_2_2.exists;
        goto _auto_loop_condition_1;
        do
        {
          {
            i_3 = _auto_next_2_2.value;
            record_4 = (RogueWindowsInputRecord){0};
            memcpy( &record_4.value, buffer+i_3, sizeof(INPUT_RECORD) );
            RogueImmediateConsoleMode___fill_event_queue_windows_process_next__RogueWindowsInputRecord( THISOBJ, record_4 );
          }
          {
            _auto_next_2_2 = RogueRangeUpToLessThanIteratorxRogueInt32x__read_another(&_auto_collection_1_1);
            _auto_condition_1_7 = _auto_next_2_2.exists;
          }
          _auto_loop_condition_1:;
        }
        while (_auto_condition_1_7);
        GetNumberOfConsoleInputEvents( h_stdin, &event_count );
        if (!!(RogueInt32)event_count)
        {
          {
            ReadConsoleInput( h_stdin, buffer, 100, &event_count );
          }
        }
      }
      {
        _auto_condition_0_6 = !!(RogueInt32)event_count;
      }
      _auto_loop_condition_0:;
    }
    while (_auto_condition_0_6);
    }
    else
    {
      GetNumberOfConsoleInputEvents( h_stdin, &event_count );
    _auto_condition_2_8 = !!(RogueInt32)event_count;
    goto _auto_loop_condition_2;
    do
    {
      {
        _auto_i_5_5 = ((RogueInt32)event_count + 1);
        if (_auto_i_5_5 > 0)
        {
          {
            _auto_condition_3_9 = --_auto_i_5_5;
            goto _auto_loop_condition_3;
            do
            {
              {
                RogueImmediateConsoleMode___fill_event_queue_windows_process_next(THISOBJ);
              }
              {
                _auto_condition_3_9 = --_auto_i_5_5;
              }
              _auto_loop_condition_3:;
            }
            while (_auto_condition_3_9);
          }
        }
        GetNumberOfConsoleInputEvents( h_stdin, &event_count );
      }
      {
        _auto_condition_2_8 = !!(RogueInt32)event_count;
      }
      _auto_loop_condition_2:;
    }
    while (_auto_condition_2_8);
    }
    #endif // ROGUE_PLATFORM_WINDOWS
  }
}

void RogueImmediateConsoleMode___fill_event_queue_windows_process_next( RogueImmediateConsoleMode* THISOBJ )
{
  RogueWindowsInputRecord record_0 = {0};
  (void)record_0;

  {
    #if defined(ROGUE_PLATFORM_WINDOWS)
    record_0 = (RogueWindowsInputRecord){0};
    HANDLE       h_stdin = GetStdHandle( STD_INPUT_HANDLE );
    DWORD        event_count = 0;
    ReadConsoleInput( h_stdin, &record_0.value, 1, &event_count );
    RogueImmediateConsoleMode___fill_event_queue_windows_process_next__RogueWindowsInputRecord( THISOBJ, record_0 );
    #endif // ROGUE_PLATFORM_WINDOWS
  }
}

void RogueImmediateConsoleMode___fill_event_queue_windows_process_next__RogueWindowsInputRecord( RogueImmediateConsoleMode* THISOBJ, RogueWindowsInputRecord record_0 )
{
  RogueInt32 unicode_1 = 0;
  (void)unicode_1;
  RogueInt32 event_flags_2 = 0;
  (void)event_flags_2;
  RogueInt32 new_button_state_3 = 0;
  (void)new_button_state_3;
  RogueInt32 x_4 = 0;
  (void)x_4;
  RogueInt32 y_5 = 0;
  (void)y_5;
  RogueInt32 delta_6 = 0;
  (void)delta_6;
  RogueInt32 toggled_7 = 0;
  (void)toggled_7;
  RogueConsoleEventList* _auto_anchored_context_0_8 = 0;
  (void)_auto_anchored_context_0_8;
  RogueConsoleEventList* _auto_anchored_context_1_9 = 0;
  (void)_auto_anchored_context_1_9;
  RogueConsoleEventList* _auto_anchored_context_2_10 = 0;
  (void)_auto_anchored_context_2_10;
  RogueConsoleEventList* _auto_anchored_context_3_11 = 0;
  (void)_auto_anchored_context_3_11;
  RogueConsoleEventList* _auto_anchored_context_4_12 = 0;
  (void)_auto_anchored_context_4_12;
  RogueConsoleEventList* _auto_anchored_context_5_13 = 0;
  (void)_auto_anchored_context_5_13;
  RogueConsoleEventList* _auto_anchored_context_6_14 = 0;
  (void)_auto_anchored_context_6_14;
  RogueConsoleEventList* _auto_anchored_context_7_15 = 0;
  (void)_auto_anchored_context_7_15;
  RogueConsoleEventList* _auto_anchored_context_8_16 = 0;
  (void)_auto_anchored_context_8_16;
  RogueConsoleEventList* _auto_anchored_context_9_17 = 0;
  (void)_auto_anchored_context_9_17;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;
  (void)_auto_local_pointer_fp_0;

  {
    #if defined(ROGUE_PLATFORM_WINDOWS)
    unicode_1 = 0;
    event_flags_2 = 0;
    new_button_state_3 = 0;
    x_4 = 0;
    y_5 = 0;
    if (record_0.value.EventType == MOUSE_EVENT)
    {
      event_flags_2 = (RogueInt32) record_0.value.Event.MouseEvent.dwEventFlags;
      new_button_state_3 = (RogueInt32) record_0.value.Event.MouseEvent.dwButtonState;
      x_4 = (RogueInt32) record_0.value.Event.MouseEvent.dwMousePosition.X;
      y_5 = (RogueInt32) record_0.value.Event.MouseEvent.dwMousePosition.Y;

      // Adjust Y coordinate to be relative to visible top-left corner of console
      CONSOLE_SCREEN_BUFFER_INFO info;
      HANDLE h_stdout = GetStdHandle( STD_OUTPUT_HANDLE );
      if (GetConsoleScreenBufferInfo(h_stdout,&info))
      {
        y_5 -= info.srWindow.Top;
      }
    if (!!event_flags_2)
    {
      {
        if (!!(event_flags_2 & DOUBLE_CLICK))
        {
          {
            RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_0_8 );
            RogueConsoleEventList__add__RogueConsoleEvent( (_auto_anchored_context_0_8=THISOBJ->events), (RogueConsoleEvent) {THISOBJ->windows_last_press_type,x_4,y_5} );
            RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_1_9 );
            RogueConsoleEventList__add__RogueConsoleEvent( (_auto_anchored_context_1_9=THISOBJ->events), (RogueConsoleEvent) {(RogueConsoleEventType) {3},x_4,y_5} );
          }
        }
        else
        {
          {
            if (!!(event_flags_2 & MOUSE_MOVED))
            {
              {
                RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_2_10 );
                RogueConsoleEventList__add__RogueConsoleEvent( (_auto_anchored_context_2_10=THISOBJ->events), (RogueConsoleEvent) {(RogueConsoleEventType) {4},x_4,y_5} );
              }
            }
            else
            {
              {
                if (!!(event_flags_2 & MOUSE_WHEELED))
                {
                  {
                    delta_6 = new_button_state_3 >> 16;
                    if (delta_6 > 0)
                    {
                      {
                        RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_3_11 );
                        RogueConsoleEventList__add__RogueConsoleEvent( (_auto_anchored_context_3_11=THISOBJ->events), (RogueConsoleEvent) {(RogueConsoleEventType) {5},x_4,y_5} );
                      }
                    }
                    else
                    {
                      {
                        {
                          {
                            RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_4_12 );
                            RogueConsoleEventList__add__RogueConsoleEvent( (_auto_anchored_context_4_12=THISOBJ->events), (RogueConsoleEvent) {(RogueConsoleEventType) {6},x_4,y_5} );
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
    else
    {
      {
        {
          {
            toggled_7 = THISOBJ->windows_button_state ^ new_button_state_3;
            if (!!toggled_7)
            {
              {
                if (!!(toggled_7 & FROM_LEFT_1ST_BUTTON_PRESSED))
                {
                  {
                    if (!!(new_button_state_3 & FROM_LEFT_1ST_BUTTON_PRESSED))
                    {
                      {
                        THISOBJ->windows_last_press_type = (RogueConsoleEventType) {1};
                        RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_5_13 );
                        RogueConsoleEventList__add__RogueConsoleEvent( (_auto_anchored_context_5_13=THISOBJ->events), (RogueConsoleEvent) {(RogueConsoleEventType) {1},x_4,y_5} );
                      }
                    }
                    else
                    {
                      {
                        {
                          {
                            RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_6_14 );
                            RogueConsoleEventList__add__RogueConsoleEvent( (_auto_anchored_context_6_14=THISOBJ->events), (RogueConsoleEvent) {(RogueConsoleEventType) {3},x_4,y_5} );
                          }
                        }
                      }
                    }
                  }
                }
                else
                {
                  {
                    if (!!(toggled_7 & RIGHTMOST_BUTTON_PRESSED))
                    {
                      {
                        if (!!(new_button_state_3 & RIGHTMOST_BUTTON_PRESSED))
                        {
                          {
                            THISOBJ->windows_last_press_type = (RogueConsoleEventType) {2};
                            RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_7_15 );
                            RogueConsoleEventList__add__RogueConsoleEvent( (_auto_anchored_context_7_15=THISOBJ->events), (RogueConsoleEvent) {(RogueConsoleEventType) {2},x_4,y_5} );
                          }
                        }
                        else
                        {
                          {
                            {
                              {
                                RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_8_16 );
                                RogueConsoleEventList__add__RogueConsoleEvent( (_auto_anchored_context_8_16=THISOBJ->events), (RogueConsoleEvent) {(RogueConsoleEventType) {3},x_4,y_5} );
                              }
                            }
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
            THISOBJ->windows_button_state = new_button_state_3;
          }
        }
      }
    }
    TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
    return;
    } // if MOUSE_EVENT

    if (record_0.value.EventType == KEY_EVENT && record_0.value.Event.KeyEvent.bKeyDown &&
        record_0.value.Event.KeyEvent.uChar.UnicodeChar)
    {
      unicode_1 = (RogueInt32) record_0.value.Event.KeyEvent.uChar.UnicodeChar;
    if (unicode_1 == 13)
    {
      {
        unicode_1 = 10;
      }
    }
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_9_17 );
    RogueConsoleEventList__add__RogueConsoleEvent( (_auto_anchored_context_9_17=THISOBJ->events), (RogueConsoleEvent) {(RogueConsoleEventType) {0},unicode_1,0} );
    }

    #endif // ROGUE_PLATFORM_WINDOWS
  }
}

void RogueImmediateConsoleMode___fill_event_queue_unix( RogueImmediateConsoleMode* THISOBJ )
{
  {
    RogueImmediateConsoleMode___fill_event_queue_unix_process_next(THISOBJ);
  }
}

void RogueImmediateConsoleMode___fill_event_queue_unix_process_next( RogueImmediateConsoleMode* THISOBJ )
{
  RogueByteList* _input_bytes_0 = 0;
  (void)_input_bytes_0;
  RogueByte b1_1 = 0;
  (void)b1_1;
  RogueConsoleEventType event_type_2 = {0};
  (void)event_type_2;
  RogueUnixConsoleMouseEventType type_3 = {0};
  (void)type_3;
  RogueInt32 x_4 = 0;
  (void)x_4;
  RogueInt32 y_5 = 0;
  (void)y_5;
  RogueInt32 ch_6 = 0;
  (void)ch_6;
  RogueInt32 ch_7 = 0;
  (void)ch_7;
  RogueConsoleEventList* _auto_anchored_context_0_8 = 0;
  (void)_auto_anchored_context_0_8;
  RogueConsoleEventList* _auto_anchored_context_1_9 = 0;
  (void)_auto_anchored_context_1_9;
  RogueConsoleEventList* _auto_anchored_context_2_10 = 0;
  (void)_auto_anchored_context_2_10;
  RogueConsoleEventList* _auto_anchored_context_3_11 = 0;
  (void)_auto_anchored_context_3_11;
  RogueConsoleEventList* _auto_anchored_context_4_12 = 0;
  (void)_auto_anchored_context_4_12;
  RogueConsoleEventList* _auto_anchored_context_5_13 = 0;
  (void)_auto_anchored_context_5_13;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    if (!RogueConsole___fill_input_buffer__RogueInt32( ROGUE_SINGLETON(RogueConsole), 1 ))
    {
      {
        TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
        return;
      }
    }
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_input_bytes_0 );
    _input_bytes_0 = ROGUE_SINGLETON(RogueConsole)->_input_bytes;
    b1_1 = RogueByteList__remove_first(_input_bytes_0);
    if (b1_1 == 27)
    {
      {
        if ((_input_bytes_0->count >= 2) && (RogueByteList__first(_input_bytes_0) == 91))
        {
          {
            RogueByteList__remove_first(_input_bytes_0);
            if (RogueByteList__first(_input_bytes_0) == 77)
            {
              {
                if (RogueConsole___fill_input_buffer__RogueInt32( ROGUE_SINGLETON(RogueConsole), 4 ))
                {
                  {
                    RogueByteList__remove_first(_input_bytes_0);
                    event_type_2 = (RogueConsoleEventType){0};
                    type_3 = (RogueUnixConsoleMouseEventType) {((RogueInt32)RogueByteList__remove_first(_input_bytes_0))};
                    switch (type_3.value)
                    {
                      case 32:
                      {
                        {
                          event_type_2 = (RogueConsoleEventType) {1};
                        }
                        break;
                      }
                      case 34:
                      {
                        {
                          event_type_2 = (RogueConsoleEventType) {2};
                        }
                        break;
                      }
                      case 35:
                      {
                        {
                          event_type_2 = (RogueConsoleEventType) {3};
                        }
                        break;
                      }
                      case 64:
                      {
                        {
                          event_type_2 = (RogueConsoleEventType) {4};
                        }
                        break;
                      }
                      case 66:
                      {
                        {
                          event_type_2 = (RogueConsoleEventType) {4};
                        }
                        break;
                      }
                      case 67:
                      {
                        {
                          event_type_2 = (RogueConsoleEventType) {4};
                        }
                        break;
                      }
                      case 96:
                      {
                        {
                          event_type_2 = (RogueConsoleEventType) {5};
                        }
                        break;
                      }
                      case 97:
                      {
                        {
                          event_type_2 = (RogueConsoleEventType) {6};
                        }
                        break;
                      }
                      default:
                      {
                        {
                          TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
                          return;
                        }
                      }
                    }
                    x_4 = ((RogueInt32)RogueByteList__remove_first(_input_bytes_0)) - 33;
                    y_5 = ((RogueInt32)RogueByteList__remove_first(_input_bytes_0)) - 33;
                    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_0_8 );
                    RogueConsoleEventList__add__RogueConsoleEvent( (_auto_anchored_context_0_8=THISOBJ->events), (RogueConsoleEvent) {event_type_2,x_4,y_5} );
                    TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
                    return;
                  }
                }
              }
            }
            else
            {
              {
                if (RogueByteList__first(_input_bytes_0) == 51)
                {
                  {
                    RogueByteList__remove_first(_input_bytes_0);
                    if (RogueConsole___fill_input_buffer__RogueInt32( ROGUE_SINGLETON(RogueConsole), 1 ))
                    {
                      {
                        if (RogueByteList__first(_input_bytes_0) == 126)
                        {
                          {
                            RogueByteList__remove_first(_input_bytes_0);
                            RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_1_9 );
                            RogueConsoleEventList__add__RogueConsoleEvent( (_auto_anchored_context_1_9=THISOBJ->events), (RogueConsoleEvent) {(RogueConsoleEventType) {0},8,0} );
                            TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
                            return;
                          }
                        }
                      }
                    }
                  }
                }
                else
                {
                  {
                    {
                      {
                        ch_6 = ((((RogueInt32)RogueByteList__remove_first(_input_bytes_0)) - 65) + 17);
                        RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_2_10 );
                        RogueConsoleEventList__add__RogueConsoleEvent( (_auto_anchored_context_2_10=THISOBJ->events), (RogueConsoleEvent) {(RogueConsoleEventType) {0},ch_6,0} );
                        TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
                        return;
                      }
                    }
                  }
                }
              }
            }
          }
        }
        else
        {
          {
            {
              {
                RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_3_11 );
                RogueConsoleEventList__add__RogueConsoleEvent( (_auto_anchored_context_3_11=THISOBJ->events), (RogueConsoleEvent) {(RogueConsoleEventType) {0},27,0} );
                TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
                return;
              }
            }
          }
        }
      }
    }
    if ((((RogueInt32)b1_1) < 192) || (!THISOBJ->decode_utf8))
    {
      {
        RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_4_12 );
        RogueConsoleEventList__add__RogueConsoleEvent( (_auto_anchored_context_4_12=THISOBJ->events), (RogueConsoleEvent) {(RogueConsoleEventType) {0},((RogueInt32)b1_1),0} );
        TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
        return;
      }
    }
    ch_7 = ((RogueInt32)b1_1);
    {
      {
        if ((((RogueInt32)b1_1) & 224) == 192)
        {
          {
            if (!(RogueConsole___fill_input_buffer__RogueInt32( ROGUE_SINGLETON(RogueConsole), 1 ))) goto _auto_unsatisfied_0;
            ch_7 = ((RogueInt32)b1_1) & 31;
            ch_7 = (ch_7 << 6) | (((RogueInt32)RogueByteList__remove_first(_input_bytes_0)) & 63);
          }
        }
        else
        {
          {
            if ((((RogueInt32)b1_1) & 240) == 224)
            {
              {
                if (!(RogueConsole___fill_input_buffer__RogueInt32( ROGUE_SINGLETON(RogueConsole), 2 ))) goto _auto_unsatisfied_0;
                ch_7 = ((RogueInt32)b1_1) & 15;
                ch_7 = (ch_7 << 6) | (((RogueInt32)RogueByteList__remove_first(_input_bytes_0)) & 63);
                ch_7 = (ch_7 << 6) | (((RogueInt32)RogueByteList__remove_first(_input_bytes_0)) & 63);
              }
            }
            else
            {
              {
                if ((((RogueInt32)b1_1) & 248) == 240)
                {
                  {
                    if (!(RogueConsole___fill_input_buffer__RogueInt32( ROGUE_SINGLETON(RogueConsole), 3 ))) goto _auto_unsatisfied_0;
                    ch_7 = ((RogueInt32)b1_1) & 7;
                    ch_7 = (ch_7 << 6) | (((RogueInt32)RogueByteList__remove_first(_input_bytes_0)) & 63);
                    ch_7 = (ch_7 << 6) | (((RogueInt32)RogueByteList__remove_first(_input_bytes_0)) & 63);
                    ch_7 = (ch_7 << 6) | (((RogueInt32)RogueByteList__remove_first(_input_bytes_0)) & 63);
                  }
                }
                else
                {
                  {
                    if ((((RogueInt32)b1_1) & 252) == 248)
                    {
                      {
                        if (!(RogueConsole___fill_input_buffer__RogueInt32( ROGUE_SINGLETON(RogueConsole), 4 ))) goto _auto_unsatisfied_0;
                        ch_7 = ((RogueInt32)b1_1) & 3;
                        ch_7 = (ch_7 << 6) | (((RogueInt32)RogueByteList__remove_first(_input_bytes_0)) & 63);
                        ch_7 = (ch_7 << 6) | (((RogueInt32)RogueByteList__remove_first(_input_bytes_0)) & 63);
                        ch_7 = (ch_7 << 6) | (((RogueInt32)RogueByteList__remove_first(_input_bytes_0)) & 63);
                        ch_7 = (ch_7 << 6) | (((RogueInt32)RogueByteList__remove_first(_input_bytes_0)) & 63);
                      }
                    }
                    else
                    {
                      {
                        {
                          {
                            if (!(RogueConsole___fill_input_buffer__RogueInt32( ROGUE_SINGLETON(RogueConsole), 5 ))) goto _auto_unsatisfied_0;
                            ch_7 = ((RogueInt32)b1_1) & 1;
                            ch_7 = (ch_7 << 6) | (((RogueInt32)RogueByteList__remove_first(_input_bytes_0)) & 63);
                            ch_7 = (ch_7 << 6) | (((RogueInt32)RogueByteList__remove_first(_input_bytes_0)) & 63);
                            ch_7 = (ch_7 << 6) | (((RogueInt32)RogueByteList__remove_first(_input_bytes_0)) & 63);
                            ch_7 = (ch_7 << 6) | (((RogueInt32)RogueByteList__remove_first(_input_bytes_0)) & 63);
                            ch_7 = (ch_7 << 6) | (((RogueInt32)RogueByteList__remove_first(_input_bytes_0)) & 63);
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
      _auto_unsatisfied_0:;
    }
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_context_5_13 );
    RogueConsoleEventList__add__RogueConsoleEvent( (_auto_anchored_context_5_13=THISOBJ->events), (RogueConsoleEvent) {(RogueConsoleEventType) {0},ch_7,0} );
  }
  TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
}

void RogueImmediateConsoleMode__init_object( RogueImmediateConsoleMode* THISOBJ )
{
  RogueConsoleEventList* _auto_obj_0_0 = 0;
  (void)_auto_obj_0_0;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    RogueConsoleMode__init_object(((RogueConsoleMode*)THISOBJ));
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_obj_0_0 );
    _auto_obj_0_0 = ROGUE_CREATE_OBJECT( RogueConsoleEventList );
    RogueConsoleEventList__init(_auto_obj_0_0);
    THISOBJ->events = _auto_obj_0_0;
    THISOBJ->decode_utf8 = 1;
  }
  TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
}

RogueString* RogueImmediateConsoleMode__type_name( RogueImmediateConsoleMode* THISOBJ )
{
  {
    RogueString* _auto_result_0 = (RogueString*)RogueObject____type_name__RogueInt32( 62 );
    return _auto_result_0;
  }
}

void RogueUnixConsoleMouseEventTypeList_gc_trace( void* THISOBJ )
{
  ((RogueObject*)THISOBJ)->__refcount = ~((RogueObject*)THISOBJ)->__refcount;
}

void RogueUnixConsoleMouseEventTypeList__init__RogueInt32( RogueUnixConsoleMouseEventTypeList* THISOBJ, RogueInt32 capacity_0 )
{
  {
    RogueUnixConsoleMouseEventTypeList__reserve__RogueInt32( THISOBJ, capacity_0 );
  }
}

void RogueUnixConsoleMouseEventTypeList__init_object( RogueUnixConsoleMouseEventTypeList* THISOBJ )
{
  {
    RogueObject__init_object(((RogueObject*)THISOBJ));
    {
      {
        THISOBJ->element_size = sizeof(THISOBJ->element_type);
      }
    }
  }
}

void RogueUnixConsoleMouseEventTypeList__on_cleanup( RogueUnixConsoleMouseEventTypeList* THISOBJ )
{
  {
    if (THISOBJ->as_bytes)
    {
      if (THISOBJ->is_borrowed) THISOBJ->is_borrowed = 0;
      else              ROGUE_FREE( THISOBJ->as_bytes );
      THISOBJ->as_bytes = 0;
      THISOBJ->capacity = 0;
      THISOBJ->count = 0;
    }
  }
}

void RogueUnixConsoleMouseEventTypeList__add__RogueUnixConsoleMouseEventType( RogueUnixConsoleMouseEventTypeList* THISOBJ, RogueUnixConsoleMouseEventType value_0 )
{
  {
    RogueUnixConsoleMouseEventTypeList__reserve__RogueInt32( THISOBJ, 1 );
    {
      {
        ((RogueUnixConsoleMouseEventType*)(THISOBJ->data))[THISOBJ->count++] = value_0;
      }
    }
  }
}

void RogueUnixConsoleMouseEventTypeList__clear( RogueUnixConsoleMouseEventTypeList* THISOBJ )
{
  {
    RogueUnixConsoleMouseEventTypeList__discard_from__RogueInt32( THISOBJ, 0 );
  }
}

RogueString* RogueUnixConsoleMouseEventTypeList__description( RogueUnixConsoleMouseEventTypeList* THISOBJ )
{
  RogueString* result_0 = 0;
  (void)result_0;
  RogueInt32 i_1 = 0;
  (void)i_1;
  RogueUnixConsoleMouseEventTypeList* _auto_collection_0_2 = 0;
  (void)_auto_collection_0_2;
  RogueInt32 _auto_count_1_3 = 0;
  (void)_auto_count_1_3;
  RogueUnixConsoleMouseEventType value_4 = {0};
  (void)value_4;
  RogueLogical _auto_condition_0_5 = 0;
  (void)_auto_condition_0_5;
  RogueString* _auto_anchored_arg_0_1_6 = 0;
  (void)_auto_anchored_arg_0_1_6;

  RogueInt32 _auto_local_pointer_fp_0 = TypeRogueObject.local_pointer_count;

  {
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &result_0 );
    result_0 = ROGUE_CREATE_OBJECT( RogueString );
    RogueString__init(result_0);
    RogueString__print__RogueCharacter( result_0, '[' );
    i_1 = 0;
    _auto_collection_0_2 = 0;
    RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_collection_0_2 );
    _auto_collection_0_2 = THISOBJ;
    _auto_count_1_3 = 0;
    _auto_count_1_3 = _auto_collection_0_2->count;
    value_4 = (RogueUnixConsoleMouseEventType){0};
    _auto_condition_0_5 = i_1 < _auto_count_1_3;
    goto _auto_loop_condition_0;
    do
    {
      {
        value_4 = RogueUnixConsoleMouseEventTypeList__get__RogueInt32( _auto_collection_0_2, i_1 );
        if (i_1 > 0)
        {
          {
            RogueString__print__RogueCharacter( result_0, ',' );
          }
        }
        _auto_anchored_arg_0_1_6 = 0;
        RogueRuntimeType_local_pointer_stack_add( &TypeRogueObject, &_auto_anchored_arg_0_1_6 );
        RogueString__print__RogueString( result_0, (_auto_anchored_arg_0_1_6=RogueUnixConsoleMouseEventType__toxRogueStringx(value_4)) );
      }
      {
        ++i_1;
        _auto_condition_0_5 = i_1 < _auto_count_1_3;
      }
      _auto_loop_condition_0:;
    }
    while (_auto_condition_0_5);
    RogueString__print__RogueCharacter( result_0, ']' );
    TypeRogueObject.local_pointer_count = _auto_local_pointer_fp_0;
    return result_0;
  }
}

void RogueUnixConsoleMouseEventTypeList__discard_from__RogueInt32( RogueUnixConsoleMouseEventTypeList* THISOBJ, RogueInt32 index_0 )
{
  RogueInt32 n_1 = 0;
  (void)n_1;

  {
    n_1 = THISOBJ->count - index_0;
    if (n_1 > 0)
    {
      {
        RogueUnixConsoleMouseEventTypeList__zero__RogueInt32_RogueOptionalInt32( THISOBJ, index_0, (RogueOptionalInt32) {n_1,1} );
        THISOBJ->count = index_0;
      }
    }
  }
}

RogueUnixConsoleMouseEventType RogueUnixConsoleMouseEventTypeList__get__RogueInt32( RogueUnixConsoleMouseEventTypeList* THISOBJ, RogueInt32 index_0 )
{
  {
    {
      {
        RogueUnixConsoleMouseEventType _auto_result_0 = ((RogueUnixConsoleMouseEventType*)(THISOBJ->data))[index_0];
        return _auto_result_0;
      }
    }
  }
}

void RogueUnixConsoleMouseEventTypeList__on_return_to_pool( RogueUnixConsoleMouseEventTypeList* THISOBJ )
{
  {
    RogueUnixConsoleMouseEventTypeList__clear(THISOBJ);
  }
}

void RogueUnixConsoleMouseEventTypeList__reserve__RogueInt32( RogueUnixConsoleMouseEventTypeList* THISOBJ, RogueInt32 additional_capacity_0 )
{
  RogueInt32 required_capacity_1 = 0;
  (void)required_capacity_1;

  {
    required_capacity_1 = (THISOBJ->count + additional_capacity_0);
    if (required_capacity_1 <= THISOBJ->capacity)
    {
      {
        return;
      }
    }
    required_capacity_1 = RogueInt32__or_larger__RogueInt32( RogueInt32__or_larger__RogueInt32( required_capacity_1, THISOBJ->count * 2 ), 10 );
    int total_size = required_capacity_1 * THISOBJ->element_size;
    RogueMM_bytes_allocated_since_gc += total_size;
    RogueByte* new_data = (RogueByte*) ROGUE_MALLOC( total_size );

    if (THISOBJ->as_bytes)
    {
      int old_size = THISOBJ->capacity * THISOBJ->element_size;
      RogueByte* old_data = THISOBJ->as_bytes;
      memcpy( new_data, old_data, old_size );
      memset( new_data+old_size, 0, total_size-old_size );
      if (THISOBJ->is_borrowed) THISOBJ->is_borrowed = 0;
      else              ROGUE_FREE( old_data );
    }
    else
    {
      memset( new_data, 0, total_size );
    }
    THISOBJ->as_bytes = new_data;
    THISOBJ->capacity = required_capacity_1;
  }
}

RogueString* RogueUnixConsoleMouseEventTypeList__toxRogueStringx( RogueUnixConsoleMouseEventTypeList* THISOBJ )
{
  {
    RogueString* _auto_result_0 = (RogueString*)RogueUnixConsoleMouseEventTypeList__description(THISOBJ);
    return _auto_result_0;
  }
}

void RogueUnixConsoleMouseEventTypeList__zero__RogueInt32_RogueOptionalInt32( RogueUnixConsoleMouseEventTypeList* THISOBJ, RogueInt32 i1_0, RogueOptionalInt32 count_1 )
{
  RogueInt32 n_2 = 0;
  (void)n_2;

  {
    n_2 = (count_1.exists ? count_1.value : THISOBJ->count);
    memset( THISOBJ->as_bytes + i1_0*THISOBJ->element_size, 0, n_2*THISOBJ->element_size );
  }
}

RogueString* RogueUnixConsoleMouseEventTypeList__type_name( RogueUnixConsoleMouseEventTypeList* THISOBJ )
{
  {
    RogueString* _auto_result_0 = (RogueString*)RogueObject____type_name__RogueInt32( 63 );
    return _auto_result_0;
  }
}

RogueRuntimeType* Rogue_types[64] =
{
   &TypeRogueLogical, &TypeRogueByte, &TypeRogueCharacter, &TypeRogueInt32,
  &TypeRogueInt64, &TypeRogueReal32, &TypeRogueReal64, &TypeRogueRogueCNativeProperty,
  &TypeRogueStackTraceFrame, &TypeRogueOptionalInt32, &TypeRogueConsoleCursor, &TypeRogueConsoleEventType,
  &TypeRogueConsoleEvent, &TypeRogueRangeUpToLessThanxRogueInt32x, &TypeRogueRangeUpToLessThanIteratorxRogueInt32x, &TypeRogueWindowsInputRecord,
  &TypeRogueUnixConsoleMouseEventType, &TypeRogueByteList, &TypeRogueString, &TypeRogueObject,
  &TypeRogueOPARENFunctionOPARENCPARENCPAREN, &TypeRogueOPARENFunctionOPARENCPARENCPARENList, &TypeRogueGlobal, &TypeRogueObject,
  &TypeRogueObject, &TypeRogueStackTraceFrameList, &TypeRogueStackTrace, &TypeRogueException,
  &TypeRogueRoutine, &TypeRogueObject, &TypeRogueObject, &TypeRogueObject,
  &TypeRogueObject, &TypeRogueObject, &TypeRogueObject, &TypeRogueCharacterList,
  &TypeRogueStringList, &TypeRogueObject, &TypeRogueStringPool, &TypeRogueObject,
  &TypeRogueObject, &TypeRogueObject, &TypeRogueObject, &TypeRogueObject,
  &TypeRogueObject, &TypeRogueObject, &TypeRogueObject, &TypeRogueObject,
  &TypeRogueObject, &TypeRogueObject, &TypeRogueObject, &TypeRogueSystem,
  &TypeRogueConsoleMode, &TypeRogueConsole, &TypeRogueObjectPoolxRogueStringx, &TypeRogueObject,
  &TypeRogueConsoleEventTypeList, &TypeRogueConsoleErrorPrinter, &TypeRogueFunction_260, &TypeRogueStandardConsoleMode,
  &TypeRogueFunction_262, &TypeRogueConsoleEventList, &TypeRogueImmediateConsoleMode, &TypeRogueUnixConsoleMouseEventTypeList
};

RogueInt32 Rogue_base_types[62] =
{
  32,28,29,33,34,10,21,42,10,20,21,10,10,10,20,9,10,20,9,10,
  20,9,36,47,10,10,10,20,9,21,10,10,20,9,10,10,38,21,48,10,
  21,10,10,20,9,10,21,48,43,10,50,10,43,10,50,10,10,20,9,10,
  20,9
};

void* Rogue_dispatch_type_name___RogueObject( void* THISOBJ )
{
  switch (((RogueObject*)THISOBJ)->__type->id)
  {
    case 8: return RogueGlobal__type_name((RogueGlobal*)THISOBJ);
    case 10: return RogueObject__type_name((RogueObject*)THISOBJ);
    case 11: return RogueString__type_name((RogueString*)THISOBJ);
    case 12: return RogueException__type_name((RogueException*)THISOBJ);
    case 14: return RogueRoutine__type_name((RogueRoutine*)THISOBJ);
    case 22: return RogueByteList__type_name((RogueByteList*)THISOBJ);
    case 23: return RogueCharacterList__type_name((RogueCharacterList*)THISOBJ);
    case 24: return RogueStringList__type_name((RogueStringList*)THISOBJ);
    case 27: return RogueStringPool__type_name((RogueStringPool*)THISOBJ);
    case 39: return RogueStackTrace__type_name((RogueStackTrace*)THISOBJ);
    case 41: return RogueStackTraceFrameList__type_name((RogueStackTraceFrameList*)THISOBJ);
    case 43: return RogueOPARENFunctionOPARENCPARENCPAREN__type_name((RogueOPARENFunctionOPARENCPARENCPAREN*)THISOBJ);
    case 44: return RogueOPARENFunctionOPARENCPARENCPARENList__type_name((RogueOPARENFunctionOPARENCPARENCPARENList*)THISOBJ);
    case 45: return RogueSystem__type_name((RogueSystem*)THISOBJ);
    case 46: return RogueConsole__type_name((RogueConsole*)THISOBJ);
    case 47: return RogueObjectPoolxRogueStringx__type_name((RogueObjectPoolxRogueStringx*)THISOBJ);
    case 50: return RogueConsoleMode__type_name((RogueConsoleMode*)THISOBJ);
    case 53: return RogueConsoleEventTypeList__type_name((RogueConsoleEventTypeList*)THISOBJ);
    case 54: return RogueConsoleErrorPrinter__type_name((RogueConsoleErrorPrinter*)THISOBJ);
    case 57: return RogueFunction_260__type_name((RogueFunction_260*)THISOBJ);
    case 58: return RogueStandardConsoleMode__type_name((RogueStandardConsoleMode*)THISOBJ);
    case 59: return RogueFunction_262__type_name((RogueFunction_262*)THISOBJ);
    case 60: return RogueImmediateConsoleMode__type_name((RogueImmediateConsoleMode*)THISOBJ);
    case 61: return RogueConsoleEventList__type_name((RogueConsoleEventList*)THISOBJ);
    default: return RogueUnixConsoleMouseEventTypeList__type_name((RogueUnixConsoleMouseEventTypeList*)THISOBJ);
  }
}

void Rogue_dispatch_flush_( void* THISOBJ )
{
  switch (((RogueObject*)THISOBJ)->__type->id)
  {
    case 8: RogueGlobal__flush((RogueGlobal*)THISOBJ); return;
    case 11: RogueString__flush((RogueString*)THISOBJ); return;
    case 46: RogueConsole__flush((RogueConsole*)THISOBJ); return;
    default: RogueConsoleErrorPrinter__flush((RogueConsoleErrorPrinter*)THISOBJ); return;
  }
}

void Rogue_dispatch_print__RogueCharacter( void* THISOBJ, RogueCharacter p0 )
{
  switch (((RogueObject*)THISOBJ)->__type->id)
  {
    case 8: RogueGlobal__print__RogueCharacter((RogueGlobal*)THISOBJ,p0); return;
    case 11: RogueString__print__RogueCharacter((RogueString*)THISOBJ,p0); return;
    case 46: RogueConsole__print__RogueCharacter((RogueConsole*)THISOBJ,p0); return;
    default: RogueConsoleErrorPrinter__print__RogueCharacter((RogueConsoleErrorPrinter*)THISOBJ,p0); return;
  }
}

void Rogue_dispatch_print__RogueString( void* THISOBJ, RogueString* p0 )
{
  switch (((RogueObject*)THISOBJ)->__type->id)
  {
    case 8: RogueGlobal__print__RogueString((RogueGlobal*)THISOBJ,p0); return;
    case 11: RogueString__print__RogueString((RogueString*)THISOBJ,p0); return;
    case 46: RogueConsole__print__RogueString((RogueConsole*)THISOBJ,p0); return;
    default: RogueConsoleErrorPrinter__print__RogueString((RogueConsoleErrorPrinter*)THISOBJ,p0); return;
  }
}

void Rogue_dispatch_println__RogueObject( void* THISOBJ, RogueObject* p0 )
{
  switch (((RogueObject*)THISOBJ)->__type->id)
  {
    case 8: RogueGlobal__println__RogueObject((RogueGlobal*)THISOBJ,p0); return;
    case 11: RogueString__println__RogueObject((RogueString*)THISOBJ,p0); return;
    case 46: RogueConsole__println__RogueObject((RogueConsole*)THISOBJ,p0); return;
    default: RogueConsoleErrorPrinter__println__RogueObject((RogueConsoleErrorPrinter*)THISOBJ,p0); return;
  }
}

void Rogue_dispatch_println__RogueString( void* THISOBJ, RogueString* p0 )
{
  switch (((RogueObject*)THISOBJ)->__type->id)
  {
    case 8: RogueGlobal__println__RogueString((RogueGlobal*)THISOBJ,p0); return;
    case 11: RogueString__println__RogueString((RogueString*)THISOBJ,p0); return;
    case 46: RogueConsole__println__RogueString((RogueConsole*)THISOBJ,p0); return;
    default: RogueConsoleErrorPrinter__println__RogueString((RogueConsoleErrorPrinter*)THISOBJ,p0); return;
  }
}

void Rogue_dispatch_on_return_to_pool_( void* THISOBJ )
{
  switch (((RogueObject*)THISOBJ)->__type->id)
  {
    case 11: RogueString__on_return_to_pool((RogueString*)THISOBJ); return;
    case 22: RogueByteList__on_return_to_pool((RogueByteList*)THISOBJ); return;
    case 23: RogueCharacterList__on_return_to_pool((RogueCharacterList*)THISOBJ); return;
    case 24: RogueStringList__on_return_to_pool((RogueStringList*)THISOBJ); return;
    case 41: RogueStackTraceFrameList__on_return_to_pool((RogueStackTraceFrameList*)THISOBJ); return;
    case 44: RogueOPARENFunctionOPARENCPARENCPARENList__on_return_to_pool((RogueOPARENFunctionOPARENCPARENCPARENList*)THISOBJ); return;
    case 53: RogueConsoleEventTypeList__on_return_to_pool((RogueConsoleEventTypeList*)THISOBJ); return;
    case 61: RogueConsoleEventList__on_return_to_pool((RogueConsoleEventList*)THISOBJ); return;
    default: RogueUnixConsoleMouseEventTypeList__on_return_to_pool((RogueUnixConsoleMouseEventTypeList*)THISOBJ); return;
  }
}

RogueString* Rogue_string_table[44];
RogueString* str____SEGFAULT___ = 0;
RogueString* str_null = 0;
RogueString* str_ = 0;
RogueString* str__9223372036854775808 = 0;
RogueString* str_Hello_Rogue_ = 0;
RogueString* str_StackTrace_init__ = 0;
RogueString* str_INTERNAL = 0;
RogueString* str_Unknown_Procedure = 0;
RogueString* str__Call_history_unavai = 0;
RogueString* str__ = 0;
RogueString* str___ = 0;
RogueString* str___1 = 0;
RogueString* str___2 = 0;
RogueString* str____ = 0;
RogueString* str____25 = 0;
RogueString* str_BACKSPACE = 0;
RogueString* str_TAB = 0;
RogueString* str_NEWLINE = 0;
RogueString* str_ESCAPE = 0;
RogueString* str_UP = 0;
RogueString* str_DOWN = 0;
RogueString* str_RIGHT = 0;
RogueString* str_LEFT = 0;
RogueString* str_DELETE = 0;
RogueString* str___3 = 0;
RogueString* str___4 = 0;
RogueString* str___5 = 0;
RogueString* str_CHARACTER = 0;
RogueString* str_POINTER_PRESS_LEFT = 0;
RogueString* str_POINTER_PRESS_RIGHT = 0;
RogueString* str_POINTER_RELEASE = 0;
RogueString* str_POINTER_MOVE = 0;
RogueString* str_SCROLL_UP = 0;
RogueString* str_SCROLL_DOWN = 0;
RogueString* str_ConsoleEventType_ = 0;
RogueString* str____1003h = 0;
RogueString* str____1003l = 0;
RogueString* str_PRESS_LEFT = 0;
RogueString* str_PRESS_RIGHT = 0;
RogueString* str_RELEASE = 0;
RogueString* str_DRAG_LEFT = 0;
RogueString* str_DRAG_RIGHT = 0;
RogueString* str_MOVE = 0;
RogueString* str_UnixConsoleMouseEven = 0;

void Rogue_clean_up(void)
{
  // Issue a few GC's to allow objects pending clean-up to do so.
  Rogue_collect_garbage();
  Rogue_collect_garbage();
  Rogue_collect_garbage();

  // Move all objects still requiring cleanup to the regular object list.
  // We cannot call on_cleanup() on them because e.g. lists could clean up
  // before objects that rely on those lists clean up.
  int n = RogueMM_objects_requiring_cleanup.count;
  RogueObject** refptr = RogueMM_objects_requiring_cleanup.data + n;
  int i;
  for (i=n; --i>=0; )
  {
    RogueObjectList_add( &RogueMM_objects, (RogueObject*) *(--refptr) );
  }
  RogueMM_objects_requiring_cleanup.count = 0;

  // Free every object on the main list
  for (i=RogueMM_objects.count, refptr=RogueMM_objects.data-1; --i>=0; )
  {
    Rogue_destroy_object( (RogueObject*) *(++refptr) );
  }
  RogueMM_objects.count = 0;

  // Reset runtime globals
  Rogue_exception = 0;
  Rogue_call_stack = 0;
  Rogue_call_stack_count = 0;
  Rogue_call_stack_capacity = 0;
  Rogue_call_stack_line = 0;

  RogueMM_bytes_allocated_since_gc = 0;
  RogueMM_gc_request = 0;
  RogueMM_objects.count = 0;
  RogueMM_objects_requiring_cleanup.count = 0;
  {
    int i;
    for (i=Rogue_type_count; --i>=0; )
    {
      RogueRuntimeType* type = Rogue_types[i];
      type->name_object = 0;
      type->local_pointer_count = 0;
      type->type_info = 0;
    }
  }
  RogueGlobal_singleton = 0;
  RogueStringPool_singleton = 0;
  RogueConsole_singleton = 0;
  RogueObjectPoolxRogueStringx_singleton = 0;
  RogueFunction_260_singleton = 0;
  RogueFunction_262_singleton = 0;
}


void  Rogue_collect_garbage( void )
{
  RogueMM_bytes_allocated_since_gc = 0;
  RogueMM_gc_request = 0;

  RogueObject*  ref;
  RogueObject** refptr;
  int i;
  int write_i;

  // Trace all retained objects (positive refcount)
  for (i=RogueMM_objects.count, refptr=RogueMM_objects.data-1; --i>=0; )
  {
    if ((ref=(RogueObject*)*(++refptr)) && ref->__refcount > 0) ref->__type->fn_gc_trace(ref);
  }

  // Trace singletons
  if ((ref = (RogueObject*)RogueGlobal_singleton) && ref->__refcount >= 0) ref->__type->fn_gc_trace(ref);
  if ((ref = (RogueObject*)RogueStringPool_singleton) && ref->__refcount >= 0) ref->__type->fn_gc_trace(ref);
  if ((ref = (RogueObject*)RogueConsole_singleton) && ref->__refcount >= 0) ref->__type->fn_gc_trace(ref);
  if ((ref = (RogueObject*)RogueObjectPoolxRogueStringx_singleton) && ref->__refcount >= 0) ref->__type->fn_gc_trace(ref);
  if ((ref = (RogueObject*)RogueFunction_260_singleton) && ref->__refcount >= 0) ref->__type->fn_gc_trace(ref);
  if ((ref = (RogueObject*)RogueFunction_262_singleton) && ref->__refcount >= 0) ref->__type->fn_gc_trace(ref);

  // Trace globals
  if ((ref = (RogueObject*)RogueSystem__g_command_line_arguments) && ref->__refcount >= 0) ref->__type->fn_gc_trace(ref);
  if ((ref = (RogueObject*)RogueSystem__g_executable_filepath) && ref->__refcount >= 0) ref->__type->fn_gc_trace(ref);
  if ((ref = (RogueObject*)RogueConsoleEventType__g_categories) && ref->__refcount >= 0) ref->__type->fn_gc_trace(ref);
  if ((ref = (RogueObject*)RogueUnixConsoleMouseEventType__g_categories) && ref->__refcount >= 0) ref->__type->fn_gc_trace(ref);

  // Trace stack
  void** v;

  for (i=TypeRogueObject.local_pointer_count, v=TypeRogueObject.local_pointer_stack-1; --i>=0; )
  {
    if ((ref=(RogueObject*)(*(RogueObject**)*(++v))) && ref->__refcount >= 0) ref->__type->fn_gc_trace(ref);
  }
  for (i=TypeRogueStackTraceFrame.local_pointer_count, v=TypeRogueStackTraceFrame.local_pointer_stack; --i>=0; ++v )
  {
    if ((ref=(RogueObject*)((*((RogueStackTraceFrame*)(*v))).procedure)) && ref->__refcount >= 0) ref->__type->fn_gc_trace(ref);
    if ((ref=(RogueObject*)((*((RogueStackTraceFrame*)(*v))).filename)) && ref->__refcount >= 0) ref->__type->fn_gc_trace(ref);
  }

  // Find unreferenced objects requiring cleanup, trace them, and move them to
  // the end of the regular list.

  // We can't delete them yet because they could add themselves back to the object
  // graph during cleanup. They'll be deleted from the regular list the next time
  // they're unreferenced with no chance to clean up again.
  int cleanup_count = 0;
  int end_i = RogueMM_objects_requiring_cleanup.count;
  refptr = RogueMM_objects_requiring_cleanup.data + end_i;
  for (i=end_i; --i>=0; )
  {
    ref = (RogueObject*) *(--refptr);
    if (ref->__refcount >= 0)
    {
      ++cleanup_count;
      ref->__type->fn_gc_trace( ref );

      RogueObjectList_add( &RogueMM_objects, ref );
      *refptr = RogueMM_objects_requiring_cleanup.data[--end_i];
    }
  }
  RogueMM_objects_requiring_cleanup.count -= cleanup_count;

  // Free unreferenced objects and reset the traced flag on referenced objects.
  for (write_i=-1,i=RogueMM_objects.count, refptr=RogueMM_objects.data-1; --i>=0; )
  {
    ref = (RogueObject*) *(++refptr);
    if (ref->__refcount >= 0)
    {
      // Untraced == unreferenced
      Rogue_destroy_object( ref );
    }
    else
    {
      // Still referenced. Reset flag.
      ref->__refcount = ~ref->__refcount;
      RogueMM_objects.data[++write_i] = ref;
    }
  }
  RogueMM_objects.count = write_i + 1;

  end_i = RogueMM_objects_requiring_cleanup.count;
  refptr = RogueMM_objects_requiring_cleanup.data + end_i;
  for (i=end_i; --i>=0; )
  {
    ref = (RogueObject*) *(--refptr);
    ref->__refcount = ~ref->__refcount;
  }

  // Call on_cleanup() the (formerly, perhaps currently) unreferenced objects
  // requiring cleanup. These are all at the end of the regular object list:
  // the last 'cleanup_count' objects on that list.
  end_i  = RogueMM_objects.count;
  refptr = RogueMM_objects.data + end_i;
  for (i=cleanup_count; --i>=0; )
  {
    ref = (RogueObject*) *(--refptr);
    ref->__type->fn_on_cleanup( ref );
  }

  //printf("New object count:%d\n",RogueMM_objects.count);

}

int Rogue_launch( void )
{
  if ( !Rogue_string_table_count )
  {
    str____SEGFAULT___ = RogueString_create_string_table_entry( "[**SEGFAULT**]");
    str_null = RogueString_create_string_table_entry( "null");
    str_ = RogueString_create_string_table_entry( "");
    str__9223372036854775808 = RogueString_create_string_table_entry( "-9223372036854775808");
    str_Hello_Rogue_ = RogueString_create_string_table_entry( "Hello Rogue!");
    str_StackTrace_init__ = RogueString_create_string_table_entry( "StackTrace.init()");
    str_INTERNAL = RogueString_create_string_table_entry( "INTERNAL");
    str_Unknown_Procedure = RogueString_create_string_table_entry( "Unknown Procedure");
    str__Call_history_unavai = RogueString_create_string_table_entry( "(Call history unavailable - compile with '--debug')");
    str__ = RogueString_create_string_table_entry( "[");
    str___ = RogueString_create_string_table_entry( "  ");
    str___1 = RogueString_create_string_table_entry( ":");
    str___2 = RogueString_create_string_table_entry( "]");
    str____ = RogueString_create_string_table_entry( "...");
    str____25 = RogueString_create_string_table_entry( """\x1B""[?25");
    str_BACKSPACE = RogueString_create_string_table_entry( "BACKSPACE");
    str_TAB = RogueString_create_string_table_entry( "TAB");
    str_NEWLINE = RogueString_create_string_table_entry( "NEWLINE");
    str_ESCAPE = RogueString_create_string_table_entry( "ESCAPE");
    str_UP = RogueString_create_string_table_entry( "UP");
    str_DOWN = RogueString_create_string_table_entry( "DOWN");
    str_RIGHT = RogueString_create_string_table_entry( "RIGHT");
    str_LEFT = RogueString_create_string_table_entry( "LEFT");
    str_DELETE = RogueString_create_string_table_entry( "DELETE");
    str___3 = RogueString_create_string_table_entry( "(");
    str___4 = RogueString_create_string_table_entry( ",");
    str___5 = RogueString_create_string_table_entry( ")");
    str_CHARACTER = RogueString_create_string_table_entry( "CHARACTER");
    str_POINTER_PRESS_LEFT = RogueString_create_string_table_entry( "POINTER_PRESS_LEFT");
    str_POINTER_PRESS_RIGHT = RogueString_create_string_table_entry( "POINTER_PRESS_RIGHT");
    str_POINTER_RELEASE = RogueString_create_string_table_entry( "POINTER_RELEASE");
    str_POINTER_MOVE = RogueString_create_string_table_entry( "POINTER_MOVE");
    str_SCROLL_UP = RogueString_create_string_table_entry( "SCROLL_UP");
    str_SCROLL_DOWN = RogueString_create_string_table_entry( "SCROLL_DOWN");
    str_ConsoleEventType_ = RogueString_create_string_table_entry( "ConsoleEventType(");
    str____1003h = RogueString_create_string_table_entry( """\x1B""[?1003h");
    str____1003l = RogueString_create_string_table_entry( """\x1B""[?1003l");
    str_PRESS_LEFT = RogueString_create_string_table_entry( "PRESS_LEFT");
    str_PRESS_RIGHT = RogueString_create_string_table_entry( "PRESS_RIGHT");
    str_RELEASE = RogueString_create_string_table_entry( "RELEASE");
    str_DRAG_LEFT = RogueString_create_string_table_entry( "DRAG_LEFT");
    str_DRAG_RIGHT = RogueString_create_string_table_entry( "DRAG_RIGHT");
    str_MOVE = RogueString_create_string_table_entry( "MOVE");
    str_UnixConsoleMouseEven = RogueString_create_string_table_entry( "UnixConsoleMouseEventType(");
  }

  RogueSystem__init_class();
  if (Rogue_exception) return 0;
  RogueConsoleCursor__init_class();
  if (Rogue_exception) return 0;
  RogueConsoleMode__init_class();
  if (Rogue_exception) return 0;
  RogueConsoleEventType__init_class();
  if (Rogue_exception) return 0;
  RogueUnixConsoleMouseEventType__init_class();
  if (Rogue_exception) return 0;

  int i;
  for (i=1; i<Rogue_argc; ++i)
  {
    RogueSystem___add_command_line_argument__RogueString(
      RogueString_create(Rogue_argv[i])
    );
  }

  RogueRoutine__on_launch();
  if (Rogue_exception) return 0;

  return 1;
}


int main( int argc, char* argv[] )
{
  Rogue_configure( argc, argv );
  Rogue_launch();
  int result = Rogue_quit();
  #if defined(ROGUE_GC_AUTO)
    Rogue_clean_up();
  #endif
  return result;
}
