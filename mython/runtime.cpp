#include "runtime.h"

#include <cassert>
#include <optional>
#include <sstream>
#include <algorithm>

using namespace std;

namespace runtime {

    ObjectHolder::ObjectHolder(std::shared_ptr<Object> data)
            : data_(std::move(data)) {
    }

    void ObjectHolder::AssertIsValid() const {
        assert(data_ != nullptr);
    }

    ObjectHolder ObjectHolder::Share(Object &object) {
        // Возвращаем невладеющий shared_ptr (его deleter ничего не делает)
        return ObjectHolder(std::shared_ptr<Object>(&object, [](auto * /*p*/) { /* do nothing */ }));
    }

    ObjectHolder ObjectHolder::None() {
        return ObjectHolder();
    }

    Object &ObjectHolder::operator*() const {
        AssertIsValid();
        return *Get();
    }

    Object *ObjectHolder::operator->() const {
        AssertIsValid();
        return Get();
    }

    Object *ObjectHolder::Get() const {
        return data_.get();
    }

    ObjectHolder::operator bool() const {
        return Get() != nullptr;
    }

    bool IsTrue(const ObjectHolder &object) {
        if (object.TryAs<Bool>()) {
            return object.TryAs<Bool>()->GetValue() == true;
        }
        if (object.TryAs<String>()) {
            return object.TryAs<String>()->GetValue().size() > 0;
        }
        if (object.TryAs<Number>()) {
            return !(object.TryAs<Number>()->GetValue() == 0);
        }
        if (object.TryAs<Class>() || object.TryAs<ClassInstance>()) {
            return false;
        }
        if (object.Get() == nullptr) {
            return false;
        }
        return true;
    }

    void ClassInstance::Print(std::ostream &os, Context &context) {
        if (HasMethod("__str__"s, 0)) {
            auto res = Call("__str__"s, {}, context);
            res->Print(os, context);
        } else {
            os << this;
        }
    }

    bool ClassInstance::HasMethod(const std::string &name, size_t argument_count) const {
        auto method = this->cls_.GetMethod(name);
        if (method && method->formal_params.size() == argument_count) {
            return true;
        }
        return false;
    }

    Closure &ClassInstance::Fields() {
        return fields_;
    }

    const Closure &ClassInstance::Fields() const {
        return fields_;
    }

    ClassInstance::ClassInstance(const Class &cls) : cls_(cls) {}

    ObjectHolder ClassInstance::Call(const std::string &method,
                                     const std::vector<ObjectHolder> &actual_args,
                                     Context &context) {
        if (HasMethod(method, actual_args.size())) {
            auto m = cls_.GetMethod(method);
            //pack ObjectHolders to Closure
            Closure closure;
            closure["self"s] = ObjectHolder::Share(*this);
            for (size_t i = 0; i < actual_args.size(); ++i) {
                closure[m->formal_params.at(i)] = actual_args.at(i);
            }
            //execute
            return m->body->Execute(closure, context);
        } else {
            throw std::runtime_error("Class has no method: "s + method);
        }
    }

    Class::Class(std::string name, std::vector<Method> methods, const Class *parent)
            : name_(name), methods_(std::move(methods)), parent_(parent) {}

    const Method *Class::GetMethod(const std::string &name) const {
        auto This = this;
        while (This != nullptr) {
            auto ptr = std::find_if(This->methods_.begin(), This->methods_.end(),
                                    [name](const Method &method) { return method.name == name; });
            if (ptr != This->methods_.end()) {
                return &*ptr;
            }
            This = This->parent_;
        }
        return nullptr;
    }

    [[nodiscard]] const std::string &Class::GetName() const {
        return this->name_;
    }

    void Class::Print(ostream &os, [[maybe_unused]] Context &context) {
        os << "Class "s << GetName();
    }

    void Bool::Print(std::ostream &os, [[maybe_unused]] Context &context) {
        os << (GetValue() ? "True"sv : "False"sv);
    }

    bool Equal(const ObjectHolder &lhs, const ObjectHolder &rhs, Context &context) {
        if (lhs.TryAs<Bool>() && rhs.TryAs<Bool>()) {
            return lhs.TryAs<Bool>()->GetValue() == rhs.TryAs<Bool>()->GetValue();
        }
        if (lhs.TryAs<String>() && rhs.TryAs<String>()) {
            return lhs.TryAs<String>()->GetValue() == rhs.TryAs<String>()->GetValue();
        }
        if (lhs.TryAs<Number>() && rhs.TryAs<Number>()) {
            return lhs.TryAs<Number>()->GetValue() == rhs.TryAs<Number>()->GetValue();
        }
        if (lhs.TryAs<ClassInstance>() && rhs.TryAs<ClassInstance>()) {
            auto r = lhs.TryAs<ClassInstance>()->Call("__eq__"s, {rhs}, context);
            return r.TryAs<Bool>()->GetValue();
        }
        if (lhs.Get() == nullptr && rhs.Get() == nullptr) {
            return true;
        }
        throw std::runtime_error("different types compared"s);
    }

    bool Less(const ObjectHolder &lhs, const ObjectHolder &rhs, Context &context) {
        if (lhs.TryAs<Bool>() && rhs.TryAs<Bool>()) {
            return lhs.TryAs<Bool>()->GetValue() < rhs.TryAs<Bool>()->GetValue();
        }
        if (lhs.TryAs<String>() && rhs.TryAs<String>()) {
            return lhs.TryAs<String>()->GetValue() < rhs.TryAs<String>()->GetValue();
        }
        if (lhs.TryAs<Number>() && rhs.TryAs<Number>()) {
            return lhs.TryAs<Number>()->GetValue() < rhs.TryAs<Number>()->GetValue();
        }
        if (lhs.TryAs<ClassInstance>() && rhs.TryAs<ClassInstance>()) {
            auto r = lhs.TryAs<ClassInstance>()->Call("__lt__"s, {rhs}, context);
            return r.TryAs<Bool>()->GetValue();
        }
        throw std::runtime_error("different types compared"s);
    }

    bool NotEqual(const ObjectHolder &lhs, const ObjectHolder &rhs, Context &context) {
        return !Equal(lhs, rhs, context);
    }

    bool Greater(const ObjectHolder &lhs, const ObjectHolder &rhs, Context &context) {
        return !(Less(lhs, rhs, context) || Equal(lhs, rhs, context));
    }

    bool LessOrEqual(const ObjectHolder &lhs, const ObjectHolder &rhs, Context &context) {
        return !Greater(lhs, rhs, context);
    }

    bool GreaterOrEqual(const ObjectHolder &lhs, const ObjectHolder &rhs, Context &context) {
        return !Less(lhs, rhs, context);
    }

}  // namespace runtime