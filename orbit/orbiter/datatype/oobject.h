// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_OOBJECT_H_
#define ORBIT_ORBITER_DATATYPE_OOBJECT_H_

#include <orbit/orbiter/context.h>

#include <orbit/orbiter/datatype/obase.h>

namespace orbiter::datatype {
    bool Equal(const OObject *left, const OObject *right);

    /**
     * @brief Add a property to a TypeInfo
     *
     * @param ctx Pointer to the Context
     * @param type Pointer to the TypeInfo
     * @param name Name of the property
     * @param value Pointer to the OObject representing the property's value
     * @param detail Additional details about the property
     *
     * @return true if property was added successfully, false otherwise
     */
    bool TIPropertyAdd(const Context *ctx, TypeInfo *type, const char *name, OObject *value, PropertyDetail detail);

    /**
     * @brief Add multiple properties(functions/methods) to a TypeInfo from a bulk definition
     *
     * @param ctx Pointer to the Context
     * @param type Pointer to the TypeInfo
     * @param bulk Pointer to the FunctionDef array containing bulk property definitions
     *
     * @return true if properties were added successfully, false otherwise
     */
    bool TIPropertyAdd(const Context *ctx, TypeInfo *type, const FunctionDef *bulk);

    /**
     * @brief Add an inline property to a TypeInfo using an offset
     *
     * @param ctx Pointer to the Context
     * @param type Pointer to the TypeInfo
     * @param name Name of the property
     * @param offset Offset of the property within the object
     * @param detail Additional details about the property
     *
     * @return true if property was added successfully, false otherwise
     */
    inline bool TIPropertyAddOffset(const Context *ctx, TypeInfo *type, const char *name,
                                    U8 offset, PropertyDetail detail) {
        return TIPropertyAdd(ctx, type, name, (OObject *) ((MSize) offset), detail | PropertyDetail::INLINE);
    }

    /**
     * @brief Initialize properties for a TypeInfo
     *
     * @param type Pointer to the TypeInfo to initialize
     * @param n Number of properties to initialize
     *
     * @return true if initialization was successful, false otherwise
     */
    bool TIPropertiesInit(TypeInfo *type, U8 n);

    MSize Hash(const OObject *obj);

    /**
     * @brief Release an OObject
     *
     * @param object Pointer to the OObject to release
     */
    inline void Release(OObject *object) {
    }

    /**
     * @brief Release an object of any type (template specialization)
     *
     * @tparam T Type of the object to release
     *
     * @param t Pointer to the object to release
     */
    template<typename T>
    inline void Release(T *t) {
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
        auto *ret = (OObject *) memory::Alloc(type->i_size);
        if (ret == nullptr)
            return nullptr;

        O_UNSAFE_GET_RC(ret) = (MSize) memory::RCType::INLINE;
        O_GET_TYPE(ret) = O_INCREF(type);

        return (T *) ret;
    }

    /**
     * @brief Create a new object of a specific type using the context's primitive type
     *
     * @tparam T Type of the object to create
     *
     * @param ctx Pointer to the Context
     * @param type Instance type of the object to create
     *
     * @return Pointer to the newly created object, or nullptr if allocation failed
     */
    template<typename T>
    T *MakeObject(const Context *ctx, InstanceType type) {
        return MakeObject<T>(ctx->primitive[(int) type]);
    }

    /**
     * @brief Create a new TypeInfo
     *
     * @param super Pointer to the superclass TypeInfo (can be nullptr)
     * @param type Instance type of the new TypeInfo
     * @param headroom Additional headroom space
     * @param props Number of properties
     * @param slots Number of slots
     *
     * @return Pointer to the newly created TypeInfo
     */
    TypeInfo *MakeType(TypeInfo *super, InstanceType type, U8 headroom, U8 props, U8 slots);

    /**
     * @brief Create a new TypeInfo without a superclass
     *
     * @param type Instance type of the new TypeInfo
     * @param headroom Additional headroom space
     * @param props Number of properties
     * @param slots Number of slots
     *
     * @return Pointer to the newly created TypeInfo
     */
    inline TypeInfo *MakeType(InstanceType type, U8 headroom, U8 props, U8 slots) {
        return MakeType((TypeInfo *) nullptr, type, headroom, props, slots);
    }

    /**
     * @brief Create a new TypeInfo using the context's 'Type' type as superclass
     *
     * @param ctx Pointer to the Context
     * @param type Instance type of the new TypeInfo
     * @param headroom Additional headroom space
     * @param props Number of properties
     * @param slots Number of slots
     *
     * @return Pointer to the newly created TypeInfo
     */
    inline TypeInfo *MakeType(const Context *ctx, InstanceType type, U8 headroom, U8 props, U8 slots) {
        return MakeType(ctx->primitive[(int) InstanceType::TYPE], type, headroom, props, slots);
    }

    // *****************************************************************************************************************
    // HANDLE
    // *****************************************************************************************************************

    template<typename T>
    class Handle {
        T *object_;

    public:
        Handle() noexcept : object_(nullptr) {
        }

        explicit Handle(T *object) noexcept : object_(object) {
        }

        Handle(Handle &&other) noexcept : object_(other.object_) {
            other.object_ = nullptr;
        }

        Handle(const Handle &) = delete;

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

                this->object_ = O_VFY_INCREF(other.object_);
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
                Release(this->object_);
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
}

#endif // !ORBIT_ORBITER_DATATYPE_OOBJECT_H_
