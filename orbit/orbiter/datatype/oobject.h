// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_OOBJECT_H_
#define ORBIT_ORBITER_DATATYPE_OOBJECT_H_

#include <orbit/orbiter/isolate.h>

#include <orbit/orbiter/memory/gc.h>

#include <orbit/orbiter/datatype/obase.h>

namespace orbiter::datatype {
    template<typename T>
    class Handle {
        T *object_;

        template<typename U>
        friend class Handle;

    public:
        Handle() noexcept : object_(nullptr) {
        }

        explicit Handle(T *object) noexcept : object_(O_INCREF(object)) {
        }

        template<typename U>
        explicit Handle(Handle<U> &&other) noexcept : object_((T *) other.object_) {
            other.object_ = nullptr;
        }

        Handle(const Handle &other) : object_(other.object_) {
            O_INCREF(other.object_);
        }

        ~Handle() noexcept {
            this->reset();
        }

        Handle &operator=(Handle &&other) noexcept {
            if (this != &other) {
                this->reset();

                this->object_ = other.object_;
                other.object_ = nullptr;
            }

            return *this;
        }

        template<typename U>
        Handle &operator=(Handle<U> &&other) noexcept {
            if (this->object_ != nullptr)
                this->reset();

            this->object_ = (T *) other.release();

            return *this;
        }

        Handle &operator=(const Handle &other) {
            if (this != &other) {
                if (this->object_ != nullptr)
                    this->reset();

                this->object_ = O_INCREF(other.object_);
            }

            return *this;
        }

        T &operator*() const { return *this->object_; }

        T *operator->() const noexcept { return this->object_; }

        explicit operator bool() const noexcept { return this->object_ != nullptr; }

        T *get() const noexcept { return this->object_; }

        T *get_inc() const noexcept { return O_INCREF(this->object_); }

        T *release() noexcept {
            T *temp = this->object_;

            this->object_ = nullptr;

            return temp;
        }

        void reset() noexcept {
            if (this->object_ != nullptr) {
                O_DECREF(this->object_);
                this->object_ = nullptr;
            }
        }

        void reset(T *new_object) noexcept {
            if (this->object_ != new_object) {
                this->reset();

                this->object_ = new_object;
            }
        }
    };

    using HOObject = Handle<OObject>;
    using HOType = Handle<TypeInfo>;

    // *****************************************************************************************************************
    // OOBJECT API
    // *****************************************************************************************************************

    bool Equal(const OObject *left, const OObject *right);

    bool EqualStrict(const OObject *left, const OObject *right);

    /**
     * @brief Checks if a type inherits or extends from a target type
     *
     * @param type Pointer to the TypeInfo representing the type to check
     * @param target Pointer to the TypeInfo representing the target type
     *
     * @return true if the type extends or inherits from the target type, false otherwise
     */
    bool IsTypeExtends(const TypeInfo *type, const TypeInfo *target);

    /**
     * @brief Add a property to a TypeInfo
     *
     * @param type Pointer to the TypeInfo
     * @param name Name of the property
     * @param value Pointer to the OObject representing the property's value 
     * @param slot Index position for the property in the object's slot array
     * @param flags Additional flags about the property
     *
     * @return true if property was added successfully, false otherwise
     */
    bool TIPropertyAdd(TypeInfo *type, const char *name, OObject *value, U16 slot, PropertyFlag flags);

    /**
     * @brief Add a property to a TypeInfo
     *
     * @param type Pointer to the TypeInfo.
     * @param name Pointer to the ORString representing the name of the property.
     * @param value Pointer to the OObject representing the property's value.
     * @param slot Index position for the property in the object's slot array
     * @param flags Additional flags about the property.
     *
     * @return true if property was added successfully, false otherwise.
     */
    bool TIPropertyAdd(TypeInfo *type, OObject *name, OObject *value, U16 slot, PropertyFlag flags);

    /**
     * @brief Add an inline property to a TypeInfo using an offset
     *
     * @param type Pointer to the TypeInfo where the property will be added.
     * @param name Pointer to the ORString representing the name of the property.
     * @param slot Index position for the property(expected 0,1,2,3... not an offset in bytes), adjusted relative to the TypeInfo.
     * @param flags Additional flags about the property.
     *
     * @return true if the property was added successfully, false otherwise.
     */
    inline bool TIPropertyAdd(TypeInfo *type, OObject *name, const U16 slot, const PropertyFlag flags) {
        return TIPropertyAdd(type, name, nullptr, slot, flags);
    }

    /**
     * @brief Adds an inline(IN_OBJECT) property to a TypeInfo
     *
     * @param type Pointer to the TypeInfo
     * @param name Pointer to the OObject representing the property's name
     * @param slot Index position for the property in the TypeInfo
     * @param flags PropertyFlag indicating additional attributes about the property
     *
     * @return true if the property was added successfully, false otherwise
     */
    inline bool TIPropertyAddInline(TypeInfo *type, OObject *name, const U16 slot, const PropertyFlag flags) {
        return TIPropertyAdd(type, name, nullptr, slot, flags | PropertyFlag::IN_OBJECT);
    }

    /**
     * @brief Add multiple properties(functions/methods) to a TypeInfo from a bulk definition
     *
     * @param type Pointer to the TypeInfo
     * @param bulk Pointer to the FunctionDef array containing bulk property definitions
     *
     * @return true if properties were added successfully, false otherwise
     */
    bool TIPropertyAdd(TypeInfo *type, const FunctionDef *bulk);

    /**
     * @brief Initialize properties for a TypeInfo
     *
     * @param isolate Pointer to the Isolate
     * @param type Pointer to the TypeInfo to initialize
     * @param n Number of properties to initialize
     *
     * @return true if initialization was successful, false otherwise
     */
    bool TIPropertiesInit(Isolate *isolate, TypeInfo *type, U8 n);

    MSize Hash(const OObject *obj);

    /**
     * @brief Release an OObject
     *
     * @param object Pointer to the OObject to release
     */
    inline void Release(OObject *object) {
        return; // TODO: stub
    }

    /**
     * @brief Release an object of any type (template specialization)
     *
     * @tparam T Type of the object to release
     *
     * @param t Pointer to the object to release
     */
    template<typename T>
    void Release(T *t) {
        Release((OObject *) t);
    }

    /**
     * @brief Find a local property in a TypeInfo
     *
     * @param type Pointer to the TypeInfo to search
     * @param name Name of the property to find
     *
     * @return Pointer to the PropertyDescriptor if found, nullptr otherwise
     */
    PropertyDescriptor *TIFindLocalProperty(const TypeInfo *type, const char *name);

    /**
     * @brief Searches for a property with the specified name within the given type and its hierarchy
     *
     * This method looks for a property with the specified name within the provided type. If the property
     * is not found in the current type, it continues the search in the type's traits (using the method
     * resolution order, MRO) or the type's parent class. If the property is found, the method can also
     * optionally return the type in which the property is explicitly located.
     *
     * @param type Pointer to the TypeInfo representing the type to search in
     * @param out_type Optional pointer to a TypeInfo pointer to store the actual type where the property is found
     * @param name The name of the property to search for
     *
     * @return A pointer to the PropertyDescriptor representing the property if found, or nullptr otherwise
     */
    PropertyDescriptor *TIFindProperty(const TypeInfo *type, const TypeInfo **out_type, const char *name);

    /**
     * @brief Creates and allocates a new object of the specified type with optional overallocation.
     *
     * @tparam T The type of the object to be created.
     * @param type Pointer to the TypeInfo describing the type of object to allocate.
     * @param overalloc Amount of additional memory to allocate beyond the size of the object.
     *
     * @return Pointer to the newly created object, or nullptr if allocation failed.
     */
    template<typename T>
    T *MakeObject(TypeInfo *type, U16 overalloc) {
        const auto *isolate = type->isolate;

        auto *ret = isolate->gc->AllocObject(type->i_size + overalloc);
        if (ret == nullptr)
            return nullptr;

        O_GET_HEAD(ret).type_ = O_INCREF(type);
        O_GET_HEAD(ret).is_instance = true;

        return (T *) ret;
    }

    /**
     * @brief Create a new object of a specific type
     *
     * @tparam T Type of the object to create
     *
     * @param type Pointer to the TypeInfo for the object
     *
     * @return Pointer to the newly created object, or nullptr if allocation failed.
     */
    template<typename T>
    T *MakeObject(TypeInfo *type) {
        return MakeObject<T>(type, 0);
    }

    /**
     * @brief Create a new object of a specific type using the isolate's primitive type
     *
     * @tparam T Type of the object to create
     *
     * @param isolate Pointer to the Isolate
     * @param type Instance type of the object to create
     *
     * @return Pointer to the newly created object, or nullptr if allocation failed.
     */
    template<typename T>
    T *MakeObject(Isolate *isolate, InstanceType type) {
        return MakeObject<T>(isolate->primitive[(int) type]);
    }

    /**
     * @brief Create a new object of a specific type using the isolate's primitive type
     *
     * @param isolate Pointer to the Isolate instance
     * @param type Instance type specifying the desired object's type
     * @param overalloc Additional memory allocation size
     *
     * @return Pointer to the newly created object, or nullptr if allocation failed.
     */
    template<typename T>
    T *MakeObject(Isolate *isolate, InstanceType type, U16 overalloc) {
        return MakeObject<T>(isolate->primitive[(int) type], overalloc);
    }

    /**
     * @brief Create a new TypeInfo
     *
     * @param isolate Pointer to the Isolate
     * @param super Pointer to the superclass TypeInfo (can be nullptr)
     * @param type Instance type of the new TypeInfo
     * @param headroom Additional headroom space
     * @param props Number of properties
     * @param slots Number of slots
     *
     * @return Pointer to the newly created TypeInfo
     */
    HOType MakeType(Isolate *isolate, TypeInfo *super, InstanceType type, U8 headroom, U8 props, U8 slots);

    /**
     * @brief Create a new TypeInfo using the isolate's 'Type' type as superclass
     *
     * @param isolate Pointer to the Isolate
     * @param type Instance type of the new TypeInfo
     * @param headroom Additional headroom space
     * @param props Number of properties
     * @param slots Number of slots
     *
     * @return Pointer to the newly created TypeInfo
     */
    inline HOType MakeType(Isolate *isolate, InstanceType type, U8 headroom, U8 props, U8 slots) {
        return MakeType(isolate, isolate->primitive[(int) InstanceType::TYPE], type, headroom, props, slots);
    }

    /**
     * @brief Create a new TypeInfo that extend a base TypeInfo.
     *
     * @param isolate Pointer to the Isolate instance managing the process.
     * @param type Instance type to specify the type information.
     * @param headroom The headroom size to allocate for the type.
     * @param props Number of properties associated with the type.
     * @param slots Number of slots used in the type definition.
     *
     * @return Pointer to the created TypeInfo.
     */
    inline HOType MakeTypeExtended(Isolate *isolate, InstanceType type, U8 headroom, U8 props, U8 slots) {
        return MakeType(isolate, isolate->primitive[(int) type], type, headroom, props, slots);
    }

    /**
     * @brief Retrieves the TypeInfo of an object
     *
     * @param object Pointer to the OObject from which the TypeInfo is to be retrieved
     *
     * @return Pointer to the TypeInfo of the object. Returns the object's type if it is an instance;
     * casts the object itself to TypeInfo otherwise.
     */
    inline TypeInfo *GetTypeInfoFromObject(const OObject *object) {
        if (O_GET_HEAD(object).is_instance)
            return O_GET_TYPE(object);

        return (TypeInfo *) object;
    }
}

#endif // !ORBIT_ORBITER_DATATYPE_OOBJECT_H_
