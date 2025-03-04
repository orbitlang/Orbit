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

    public:
        Handle() noexcept: object_(nullptr) {
        }

        explicit Handle(T *object) noexcept: object_(O_INCREF(object)) {
        }

        Handle(Handle &&other) noexcept: object_(other.object_) {
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

    /**
     * @brief Add a property to a TypeInfo
     *
     * @param type Pointer to the TypeInfo
     * @param name Name of the property
     * @param value Pointer to the OObject representing the property's value
     * @param flags Additional flags about the property
     *
     * @return true if property was added successfully, false otherwise
     */
    bool TIPropertyAdd(TypeInfo *type, const char *name, OObject *value, PropertyFlag flags);

    /**
     * @brief Add an inline property to a TypeInfo using an offset
     *
     * @param type Pointer to the TypeInfo
     * @param name Name of the property
     * @param offset Offset of the property(expected 0,1,2,3... not an offset in bytes), adjusted relative to the TypeInfo.
     * @param flags Additional flags about the property
     *
     * @return true if property was added successfully, false otherwise
     */
    inline bool TIPropertyAdd(TypeInfo *type, const char *name, U16 offset, PropertyFlag flags) {
        offset = type->offset + type->headroom + (offset * sizeof(void *));
        return TIPropertyAdd(type, name, (OObject *) offset, flags | PropertyFlag::IN_OBJECT);
    }

    /**
     * @brief Add a property to a TypeInfo
     *
     * @param type Pointer to the TypeInfo.
     * @param name Pointer to the ORString representing the name of the property.
     * @param value Pointer to the OObject representing the property's value.
     * @param flags Additional flags about the property.
     *
     * @return true if property was added successfully, false otherwise.
     */
    bool TIPropertyAdd(TypeInfo *type, OObject *name, OObject *value, PropertyFlag flags);

    /**
     * @brief Add an inline property to a TypeInfo using an offset
     *
     * @param type Pointer to the TypeInfo where the property will be added.
     * @param name Pointer to the ORString representing the name of the property.
     * @param offset Offset of the property(expected 0,1,2,3... not an offset in bytes), adjusted relative to the TypeInfo.
     * @param flags Additional flags about the property.
     *
     * @return true if the property was added successfully, false otherwise.
     */
    inline bool TIPropertyAdd(TypeInfo *type, OObject *name, U16 offset, PropertyFlag flags) {
        return TIPropertyAdd(type, name, (OObject *) offset, flags | PropertyFlag::IN_OBJECT);
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
     * @brief Find a property in a TypeInfo or its superclasses
     *
     * @param type Pointer to the TypeInfo to search
     * @param name Name of the property to find
     *
     * @return Pointer to the PropertyDescriptor if found, nullptr otherwise
     */
    PropertyDescriptor *TIFindProperty(const TypeInfo *type, const char *name);

    /**
     * @brief Create a new object of a specific type
     *
     * @tparam T Type of the object to create
     *
     * @param type Pointer to the TypeInfo for the object
     *
     * @return Pointer to the newly created object, or nullptr if allocation failed
     */
    template<typename T>
    T *MakeObject(TypeInfo *type) {
        auto *isolate = O_GET_ISOLATE(type);

        auto *ret = isolate->gc->AllocObject(type->i_size);
        if (ret == nullptr)
            return nullptr;

        O_GET_TYPE(ret) = O_INCREF(type);

        return (T *) ret;
    }

    /**
     * @brief Create a new object of a specific type using the isolate's primitive type
     *
     * @tparam T Type of the object to create
     *
     * @param isolate Pointer to the Isolate
     * @param type Instance type of the object to create
     *
     * @return Pointer to the newly created object, or nullptr if allocation failed
     */
    template<typename T>
    T *MakeObject(Isolate *isolate, InstanceType type) {
        return MakeObject<T>(isolate->primitive[(int) type]);
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
}

#endif // !ORBIT_ORBITER_DATATYPE_OOBJECT_H_
