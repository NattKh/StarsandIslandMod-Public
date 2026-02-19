// IL2CPP Runtime API - resolved dynamically from GameAssembly.dll
#pragma once
#include <windows.h>
#include <psapi.h>
#include <stdio.h>

// ============================================================================
// IL2CPP Type Definitions
// ============================================================================

typedef void Il2CppDomain;
typedef void Il2CppAssembly;
typedef void Il2CppImage;
typedef void Il2CppClass;
typedef void Il2CppType;
typedef void Il2CppMethodInfo;
typedef void Il2CppFieldInfo;
typedef void Il2CppPropertyInfo;
typedef void Il2CppThread;

// Il2CppObject: all managed objects have this header
typedef struct Il2CppObject {
    void* klass;   // Il2CppClass*
    void* monitor; // MonitorData*
} Il2CppObject;

// Il2CppString: System.String
typedef struct Il2CppString {
    Il2CppObject object;
    int length;
    wchar_t chars[1]; // variable-length
} Il2CppString;

// Il2CppArray
typedef struct Il2CppArray {
    Il2CppObject object;
    void* bounds;
    size_t max_length;
    // elements follow
} Il2CppArray;

// ============================================================================
// IL2CPP API Function Pointers
// ============================================================================

// Domain / Init
typedef Il2CppDomain*   (*fn_il2cpp_domain_get)(void);
typedef Il2CppAssembly** (*fn_il2cpp_domain_get_assemblies)(Il2CppDomain*, size_t*);
typedef Il2CppThread*   (*fn_il2cpp_thread_attach)(Il2CppDomain*);
typedef void            (*fn_il2cpp_thread_detach)(Il2CppThread*);

// Assembly / Image
typedef Il2CppImage*    (*fn_il2cpp_assembly_get_image)(Il2CppAssembly*);
typedef const char*     (*fn_il2cpp_image_get_name)(Il2CppImage*);
typedef size_t          (*fn_il2cpp_image_get_class_count)(Il2CppImage*);
typedef Il2CppClass*    (*fn_il2cpp_image_get_class)(Il2CppImage*, size_t);

// Class
typedef Il2CppClass*    (*fn_il2cpp_class_from_name)(Il2CppImage*, const char* namespaze, const char* name);
typedef const char*     (*fn_il2cpp_class_get_name)(Il2CppClass*);
typedef const char*     (*fn_il2cpp_class_get_namespace)(Il2CppClass*);
typedef Il2CppMethodInfo* (*fn_il2cpp_class_get_method_from_name)(Il2CppClass*, const char*, int argcount);
typedef Il2CppMethodInfo* (*fn_il2cpp_class_get_methods)(Il2CppClass*, void** iter);
typedef Il2CppFieldInfo*  (*fn_il2cpp_class_get_field_from_name)(Il2CppClass*, const char*);
typedef Il2CppFieldInfo*  (*fn_il2cpp_class_get_fields)(Il2CppClass*, void** iter);
typedef Il2CppPropertyInfo* (*fn_il2cpp_class_get_property_from_name)(Il2CppClass*, const char*);
typedef Il2CppClass*    (*fn_il2cpp_class_get_parent)(Il2CppClass*);
typedef Il2CppType*     (*fn_il2cpp_class_get_type)(Il2CppClass*);
typedef int             (*fn_il2cpp_class_instance_size)(Il2CppClass*);
typedef void*           (*fn_il2cpp_class_get_static_field_data)(Il2CppClass*);
typedef void            (*fn_il2cpp_runtime_class_init)(Il2CppClass*);
typedef int             (*fn_il2cpp_class_is_valuetype)(Il2CppClass*);

// Method
typedef const char*     (*fn_il2cpp_method_get_name)(Il2CppMethodInfo*);
typedef int             (*fn_il2cpp_method_get_param_count)(Il2CppMethodInfo*);
typedef Il2CppClass*    (*fn_il2cpp_method_get_class)(Il2CppMethodInfo*);

// Field
typedef const char*     (*fn_il2cpp_field_get_name)(Il2CppFieldInfo*);
typedef void            (*fn_il2cpp_field_get_value)(Il2CppObject*, Il2CppFieldInfo*, void*);
typedef void            (*fn_il2cpp_field_set_value)(Il2CppObject*, Il2CppFieldInfo*, void*);
typedef void            (*fn_il2cpp_field_static_get_value)(Il2CppFieldInfo*, void*);
typedef void            (*fn_il2cpp_field_static_set_value)(Il2CppFieldInfo*, void*);
typedef size_t          (*fn_il2cpp_field_get_offset)(Il2CppFieldInfo*);

// Object
typedef Il2CppObject*   (*fn_il2cpp_object_new)(Il2CppClass*);
typedef Il2CppObject*   (*fn_il2cpp_runtime_invoke)(Il2CppMethodInfo*, void* obj, void** params, Il2CppObject** exc);
typedef void*           (*fn_il2cpp_object_unbox)(Il2CppObject*);

// String
typedef Il2CppString*   (*fn_il2cpp_string_new)(const char*);
typedef Il2CppString*   (*fn_il2cpp_string_new_utf16)(const wchar_t*, int len);
typedef wchar_t*        (*fn_il2cpp_string_chars)(Il2CppString*);
typedef int             (*fn_il2cpp_string_length)(Il2CppString*);

// Array
typedef Il2CppArray*    (*fn_il2cpp_array_new)(Il2CppClass*, size_t);
typedef size_t          (*fn_il2cpp_array_length)(Il2CppArray*);

// Type
typedef Il2CppClass*    (*fn_il2cpp_type_get_class_or_element_class)(Il2CppType*);
typedef const char*     (*fn_il2cpp_type_get_name)(Il2CppType*);

// ============================================================================
// Global API table
// ============================================================================

static struct {
    fn_il2cpp_domain_get                    domain_get;
    fn_il2cpp_domain_get_assemblies         domain_get_assemblies;
    fn_il2cpp_thread_attach                 thread_attach;
    fn_il2cpp_thread_detach                 thread_detach;

    fn_il2cpp_assembly_get_image            assembly_get_image;
    fn_il2cpp_image_get_name                image_get_name;
    fn_il2cpp_image_get_class_count         image_get_class_count;
    fn_il2cpp_image_get_class               image_get_class;

    fn_il2cpp_class_from_name               class_from_name;
    fn_il2cpp_class_get_name                class_get_name;
    fn_il2cpp_class_get_namespace           class_get_namespace;
    fn_il2cpp_class_get_method_from_name    class_get_method_from_name;
    fn_il2cpp_class_get_methods             class_get_methods;
    fn_il2cpp_class_get_field_from_name     class_get_field_from_name;
    fn_il2cpp_class_get_fields              class_get_fields;
    fn_il2cpp_class_get_property_from_name  class_get_property_from_name;
    fn_il2cpp_class_get_parent              class_get_parent;
    fn_il2cpp_class_get_type                class_get_type;
    fn_il2cpp_class_instance_size           class_instance_size;
    fn_il2cpp_class_get_static_field_data   class_get_static_field_data;
    fn_il2cpp_runtime_class_init            runtime_class_init;
    fn_il2cpp_class_is_valuetype            class_is_valuetype;

    fn_il2cpp_method_get_name               method_get_name;
    fn_il2cpp_method_get_param_count        method_get_param_count;
    fn_il2cpp_method_get_class              method_get_class;

    fn_il2cpp_field_get_name                field_get_name;
    fn_il2cpp_field_get_value               field_get_value;
    fn_il2cpp_field_set_value               field_set_value;
    fn_il2cpp_field_static_get_value        field_static_get_value;
    fn_il2cpp_field_static_set_value        field_static_set_value;
    fn_il2cpp_field_get_offset              field_get_offset;

    fn_il2cpp_object_new                    object_new;
    fn_il2cpp_runtime_invoke                runtime_invoke;
    fn_il2cpp_object_unbox                  object_unbox;

    fn_il2cpp_string_new                    string_new;
    fn_il2cpp_string_new_utf16              string_new_utf16;
    fn_il2cpp_string_chars                  string_chars;
    fn_il2cpp_string_length                 string_length;

    fn_il2cpp_array_new                     array_new;
    fn_il2cpp_array_length                  array_length;

    fn_il2cpp_type_get_class_or_element_class type_get_class_or_element_class;
    fn_il2cpp_type_get_name                 type_get_name;

    HMODULE hGameAssembly;
} IL2CPP = {0};

#define RESOLVE(name) IL2CPP.name = (fn_il2cpp_##name)GetProcAddress(IL2CPP.hGameAssembly, "il2cpp_" #name)

static BOOL IL2CPP_Init(void) {
    // Try multiple methods to find GameAssembly.dll:

    // Method 1: GetModuleHandle (standard)
    IL2CPP.hGameAssembly = GetModuleHandleA("GameAssembly.dll");

    // Method 2: GetModuleHandle with full path
    if (!IL2CPP.hGameAssembly) {
        char exePath[MAX_PATH];
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        char* slash = strrchr(exePath, '\\');
        if (slash) {
            *(slash + 1) = 0;
            strcat(exePath, "GameAssembly.dll");
            IL2CPP.hGameAssembly = GetModuleHandleA(exePath);
        }
    }

    // Method 3: Enumerate all modules and find by name
    if (!IL2CPP.hGameAssembly) {
        HMODULE mods[1024];
        DWORD needed;
        HANDLE proc = GetCurrentProcess();
        if (EnumProcessModules(proc, mods, sizeof(mods), &needed)) {
            int count = needed / sizeof(HMODULE);
            for (int i = 0; i < count; i++) {
                char name[MAX_PATH];
                if (GetModuleFileNameA(mods[i], name, MAX_PATH)) {
                    // Case-insensitive check for GameAssembly
                    char* base = strrchr(name, '\\');
                    if (base) base++; else base = name;
                    if (_stricmp(base, "GameAssembly.dll") == 0) {
                        IL2CPP.hGameAssembly = mods[i];
                        break;
                    }
                }
            }
        }
    }

    if (!IL2CPP.hGameAssembly) return FALSE;

    RESOLVE(domain_get);
    RESOLVE(domain_get_assemblies);
    RESOLVE(thread_attach);
    RESOLVE(thread_detach);

    RESOLVE(assembly_get_image);
    RESOLVE(image_get_name);
    RESOLVE(image_get_class_count);
    RESOLVE(image_get_class);

    RESOLVE(class_from_name);
    RESOLVE(class_get_name);
    RESOLVE(class_get_namespace);
    RESOLVE(class_get_method_from_name);
    RESOLVE(class_get_methods);
    RESOLVE(class_get_field_from_name);
    RESOLVE(class_get_fields);
    RESOLVE(class_get_property_from_name);
    RESOLVE(class_get_parent);
    RESOLVE(class_get_type);
    RESOLVE(class_instance_size);
    RESOLVE(class_get_static_field_data);
    RESOLVE(runtime_class_init);
    RESOLVE(class_is_valuetype);

    RESOLVE(method_get_name);
    RESOLVE(method_get_param_count);
    RESOLVE(method_get_class);

    RESOLVE(field_get_name);
    RESOLVE(field_get_value);
    RESOLVE(field_set_value);
    RESOLVE(field_static_get_value);
    RESOLVE(field_static_set_value);
    RESOLVE(field_get_offset);

    RESOLVE(object_new);
    RESOLVE(runtime_invoke);
    RESOLVE(object_unbox);

    RESOLVE(string_new);
    RESOLVE(string_new_utf16);
    RESOLVE(string_chars);
    RESOLVE(string_length);

    RESOLVE(array_new);
    RESOLVE(array_length);

    RESOLVE(type_get_class_or_element_class);
    RESOLVE(type_get_name);

    return (IL2CPP.domain_get != NULL);
}

// ============================================================================
// Convenience helpers
// ============================================================================

// Find a class by assembly name, namespace, and class name
static Il2CppClass* IL2CPP_FindClass(const char* assemblyName, const char* namespaze, const char* className) {
    Il2CppDomain* domain = IL2CPP.domain_get();
    size_t count = 0;
    Il2CppAssembly** assemblies = IL2CPP.domain_get_assemblies(domain, &count);

    for (size_t i = 0; i < count; i++) {
        Il2CppImage* image = IL2CPP.assembly_get_image(assemblies[i]);
        const char* imgName = IL2CPP.image_get_name(image);

        // Match assembly name (with or without .dll)
        if (assemblyName) {
            if (_stricmp(imgName, assemblyName) != 0) {
                char buf[256];
                snprintf(buf, sizeof(buf), "%s.dll", assemblyName);
                if (_stricmp(imgName, buf) != 0) continue;
            }
        }

        Il2CppClass* klass = IL2CPP.class_from_name(image, namespaze ? namespaze : "", className);
        if (klass) return klass;
    }
    return NULL;
}

// Invoke a static method with no args
static Il2CppObject* IL2CPP_InvokeStatic(Il2CppClass* klass, const char* methodName, int argCount, void** args) {
    Il2CppMethodInfo* method = IL2CPP.class_get_method_from_name(klass, methodName, argCount);
    if (!method) return NULL;
    Il2CppObject* exc = NULL;
    return IL2CPP.runtime_invoke(method, NULL, args, &exc);
}

// Invoke an instance method
static Il2CppObject* IL2CPP_InvokeMethod(Il2CppObject* obj, const char* methodName, int argCount, void** args) {
    if (!obj) return NULL;
    Il2CppClass* klass = *(Il2CppClass**)obj; // klass is the first field
    Il2CppMethodInfo* method = IL2CPP.class_get_method_from_name(klass, methodName, argCount);
    if (!method) return NULL;
    Il2CppObject* exc = NULL;
    return IL2CPP.runtime_invoke(method, obj, args, &exc);
}
