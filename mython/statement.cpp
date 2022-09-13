#include "statement.h"

#include <iostream>
#include <sstream>

using namespace std;

namespace ast {

    using runtime::Closure;
    using runtime::Context;
    using runtime::ObjectHolder;

    namespace {
        const string ADD_METHOD = "__add__"s;
        const string INIT_METHOD = "__init__"s;
    }  // namespace

    ObjectHolder Assignment::Execute(Closure &closure, Context &context) {
        closure[var_] = value_->Execute(closure, context);
        return closure[var_];
    }

    Assignment::Assignment(std::string var, std::unique_ptr<Statement> rv)
            : var_(std::move(var)), value_(std::move(rv)) {}

    VariableValue::VariableValue(const std::string &var_name) {
        dotted_ids_.push_back(std::move(var_name));
    }

    VariableValue::VariableValue(std::vector<std::string> dotted_ids) : dotted_ids_(std::move(dotted_ids)) {}

    ObjectHolder VariableValue::Execute(Closure &closure, [[maybe_unused]] Context &context) {
        if (dotted_ids_.empty()) {
            throw std::runtime_error("No parameters for execute"s);
        }
        if (dotted_ids_.size() > 1) {
            //это dotted id. Все кроме последнего значения - это обьекты хранимые в closure
            ObjectHolder object = closure[dotted_ids_[0]];
            ObjectHolder val;
            for (size_t i = 1; i < dotted_ids_.size(); ++i) {
                val = object.TryAs<runtime::ClassInstance>()->Fields()[dotted_ids_[i]];
                object = closure[dotted_ids_[i]];
            }
            return val;
        }
        //это простая переменная - она одна в dotted_ids_ и хранится в closure
        if (closure.count(dotted_ids_[0]) == 0) {
            throw std::runtime_error("Variable \'"s + dotted_ids_[0] + " not found"s);
        }
        return closure[dotted_ids_[0]];
    }

    unique_ptr<Print> Print::Variable(const std::string &name) {
        return std::make_unique<Print>(std::make_unique<VariableValue>(std::move(name)));
    }

    Print::Print(unique_ptr<Statement> argument) {
        args_.emplace_back(std::move(argument));
    }

    Print::Print(vector<unique_ptr<Statement>> args) : args_(std::move(args)) {}

    ObjectHolder Print::Execute(Closure &closure, Context &context) {
        // Во время выполнения команды print вывод должен осуществляться в поток, возвращаемый из
        // context.GetOutputStream()
        auto &out = context.GetOutputStream();
        runtime::DummyContext context_local;
        bool first = true;
        for (const auto &arg : args_) {
            ObjectHolder obj = arg->Execute(closure, context);
            if (first) {
                first = false;
            } else {
                out << ' ';
            }
            if (!obj) {
                out << "None"s;
            } else {
                obj->Print(out, context_local);
            }
        }
        out << '\n';
        return runtime::ObjectHolder::None();
    }

    MethodCall::MethodCall(std::unique_ptr<Statement> object, std::string method,
                           std::vector<std::unique_ptr<Statement>> args)
            : object_(std::move(object)), method_(std::move(method)), args_(std::move(args)) {}

    // Вызывает метод object.method со списком параметров args
    ObjectHolder MethodCall::Execute(Closure &closure, Context &context) {
        auto obj = object_->Execute(closure, context);
        if (obj && obj.TryAs<runtime::ClassInstance>()->HasMethod(method_, args_.size())) {
            std::vector<ObjectHolder> actual_args;
            for (const auto &arg : args_) {
                actual_args.emplace_back(arg->Execute(closure, context));
            }
            return obj.TryAs<runtime::ClassInstance>()->Call(method_, actual_args, context);
        }
        throw std::runtime_error("Method: " + method_ + " call error"s);
    }

    ObjectHolder Stringify::Execute(Closure &closure, Context &context) {
        if (argument_) {
            std::ostringstream str;
            auto obj = argument_->Execute(closure, context);
            if (obj) {
                obj->Print(str, context);
            } else {
                str << "None"s;
            }
            return runtime::ObjectHolder::Own(runtime::String(str.str()));
        }
        return runtime::ObjectHolder::None();
    }

    // Поддерживается сложение:
    //  число + число
    //  строка + строка
    //  объект1 + объект2, если у объект1 - пользовательский класс с методом _add__(rhs)
    // В противном случае при вычислении выбрасывается runtime_error
    ObjectHolder Add::Execute(Closure &closure, Context &context) {
        auto lhs = lhs_->Execute(closure, context);
        auto rhs = rhs_->Execute(closure, context);
        if (lhs.TryAs<runtime::String>() && rhs.TryAs<runtime::String>()) {
            return runtime::ObjectHolder::Own(runtime::String(
                    lhs.TryAs<runtime::String>()->GetValue() + rhs.TryAs<runtime::String>()->GetValue()));
        }
        if (lhs.TryAs<runtime::Number>() && rhs.TryAs<runtime::Number>()) {
            return runtime::ObjectHolder::Own(runtime::Number(
                    lhs.TryAs<runtime::Number>()->GetValue() + rhs.TryAs<runtime::Number>()->GetValue()));
        }
        if (lhs.TryAs<runtime::ClassInstance>()) {
            return lhs.TryAs<runtime::ClassInstance>()->Call(ADD_METHOD, {rhs}, context);
        }
        throw std::runtime_error("Can\'t Add different types lhs and rhs"s);
    }

    // Поддерживается вычитание:
    //  число - число
    // Если lhs и rhs - не числа, выбрасывается исключение runtime_error
    ObjectHolder Sub::Execute(Closure &closure, Context &context) {
        auto lhs = lhs_->Execute(closure, context);
        auto rhs = rhs_->Execute(closure, context);
        if (lhs.TryAs<runtime::Number>() && rhs.TryAs<runtime::Number>()) {
            return runtime::ObjectHolder::Own(runtime::Number(
                    lhs.TryAs<runtime::Number>()->GetValue() - rhs.TryAs<runtime::Number>()->GetValue()));
        }
        throw std::runtime_error("Sub can operate with numbers only"s);
    }

    // Поддерживается умножение:
    //  число * число
    // Если lhs и rhs - не числа, выбрасывается исключение runtime_error
    ObjectHolder Mult::Execute(Closure &closure, Context &context) {
        auto lhs = lhs_->Execute(closure, context);
        auto rhs = rhs_->Execute(closure, context);
        if (lhs.TryAs<runtime::Number>() && rhs.TryAs<runtime::Number>()) {
            return runtime::ObjectHolder::Own(runtime::Number(
                    lhs.TryAs<runtime::Number>()->GetValue() * rhs.TryAs<runtime::Number>()->GetValue()));
        }
        throw std::runtime_error("Mult can operate with numbers only"s);
    }

    // Поддерживается деление:
    //  число / число
    // Если lhs и rhs - не числа, выбрасывается исключение runtime_error
    // Если rhs равен 0, выбрасывается исключение runtime_error
    ObjectHolder Div::Execute(Closure &closure, Context &context) {
        auto lhs = lhs_->Execute(closure, context);
        auto rhs = rhs_->Execute(closure, context);
        if (!lhs.TryAs<runtime::Number>() or !rhs.TryAs<runtime::Number>()) {
            throw std::runtime_error("Sub can operate with numbers only"s);
        }
        if (rhs.TryAs<runtime::Number>()->GetValue() == 0) {
            throw std::runtime_error("Division by zero"s);
        }
        return runtime::ObjectHolder::Own(runtime::Number(
                lhs.TryAs<runtime::Number>()->GetValue() / rhs.TryAs<runtime::Number>()->GetValue()));

    }

    ObjectHolder Compound::Execute(Closure &closure, Context &context) {
        for (const auto &stm : statements_) {
            stm->Execute(closure, context);
        }
        return runtime::ObjectHolder::None();
    }

    void Compound::AddStatement(std::unique_ptr<Statement> stmt) {
        statements_.emplace_back(std::move(stmt));
    }

    ObjectHolder Return::Execute(Closure &closure, Context &context) {
        throw statement_->Execute(closure, context);
    }

    Return::Return(std::unique_ptr<Statement> statement) : statement_(std::move(statement)) {}

    ClassDefinition::ClassDefinition(ObjectHolder cls) : cls_(std::move(cls)) {}

    // Создаёт внутри closure новый объект, совпадающий с именем класса и значением, переданным в
    // конструктор
    ObjectHolder ClassDefinition::Execute(Closure &closure, [[maybe_unused]] Context &context) {
        closure.emplace(cls_.TryAs<runtime::Class>()->GetName(), std::move(cls_));
        return runtime::ObjectHolder::None();
    }

    FieldAssignment::FieldAssignment(VariableValue object, std::string field_name,
                                     std::unique_ptr<Statement> rv)
            : object_(std::move(object)), field_name_(std::move(field_name)), value_(std::move(rv)) {}

    ObjectHolder FieldAssignment::Execute(Closure &closure, Context &context) {
        Closure &fields = object_.Execute(closure, context).TryAs<runtime::ClassInstance>()->Fields();
        fields[field_name_] = value_->Execute(closure, context);
        return fields[field_name_];
    }

    IfElse::IfElse(std::unique_ptr<Statement> condition, std::unique_ptr<Statement> if_body,
                   std::unique_ptr<Statement> else_body)
            : condition_(std::move(condition)), if_body_(std::move(if_body)), else_body_(std::move(else_body)) {}

    // Инструкция if <condition> <if_body> else <else_body>
    // Параметр else_body может быть равен nullptr
    ObjectHolder IfElse::Execute(Closure & closure, Context & context) {
        if (runtime::IsTrue(condition_->Execute(closure, context))) {
            return if_body_->Execute(closure, context);
        } else if (else_body_) {
            return else_body_->Execute(closure, context);
        }
        return runtime::ObjectHolder::None();
    }

    ObjectHolder Or::Execute(Closure &closure, Context &context) {
        // Значение аргумента rhs вычисляется, только если значение lhs
        // после приведения к Bool равно False
        auto lhs = lhs_->Execute(closure, context);
        auto rhs = rhs_->Execute(closure, context);
        if (runtime::IsTrue(lhs) || runtime::IsTrue(rhs)) {
            return runtime::ObjectHolder::Own(runtime::Bool(true));
        }
        return runtime::ObjectHolder::Own(runtime::Bool(false));
    }

    ObjectHolder And::Execute(Closure &closure, Context &context) {
        // Значение аргумента rhs вычисляется, только если значение lhs
        // после приведения к Bool равно False
        auto lhs = lhs_->Execute(closure, context);
        auto rhs = rhs_->Execute(closure, context);
        if (runtime::IsTrue(lhs) && runtime::IsTrue(rhs)) {
            return runtime::ObjectHolder::Own(runtime::Bool(true));
        }
        return runtime::ObjectHolder::Own(runtime::Bool(false));
    }

    ObjectHolder Not::Execute(Closure &closure, Context &context) {
        auto argument = argument_->Execute(closure, context);
        return runtime::ObjectHolder::Own(runtime::Bool(!runtime::IsTrue(argument)));
    }

    Comparison::Comparison(Comparator cmp, unique_ptr<Statement> lhs, unique_ptr<Statement> rhs)
            : BinaryOperation(std::move(lhs), std::move(rhs)), cmp_(std::move(cmp)) {}

    ObjectHolder Comparison::Execute(Closure &closure, Context &context) {
        return runtime::ObjectHolder::Own(
                runtime::Bool(cmp_(lhs_->Execute(closure, context),
                                   rhs_->Execute(closure, context),
                                   context)));
    }

    NewInstance::NewInstance(const runtime::Class &class_, std::vector<std::unique_ptr<Statement>> args)
            : class_(class_), args_(std::move(args)) {}

    NewInstance::NewInstance(const runtime::Class &class_)
            : NewInstance(class_, {}) {}

    ObjectHolder NewInstance::Execute(Closure &closure, Context &context) {
        //runtime::ClassInstance instance{class_};
        auto* instance = new runtime::ClassInstance(class_);
        if (instance->HasMethod(INIT_METHOD, args_.size())) {
            std::vector<ObjectHolder> actual_args;
            for (const auto &arg : args_) {
                actual_args.emplace_back(arg->Execute(closure, context));
            }
            //execute constructor
            instance->Call(INIT_METHOD, actual_args, context);
        }
        //return ObjectHolder::Own(std::move(instance));
        return ObjectHolder::Share(*instance);
    }

    MethodBody::MethodBody(std::unique_ptr<Statement> &&body) : body_(std::move(body)) {}

    ObjectHolder MethodBody::Execute(Closure &closure, Context &context) {
        try {
            body_->Execute(closure, context);
        }
        catch (ObjectHolder res) {
            return res;
        }
        return runtime::ObjectHolder::None();
    }

    UnaryOperation::UnaryOperation(std::unique_ptr<Statement> argument) : argument_(std::move(argument)) {}

    BinaryOperation::BinaryOperation(std::unique_ptr<Statement> lhs, std::unique_ptr<Statement> rhs)
            : lhs_(std::move(lhs)), rhs_(std::move(rhs)) {}
}  // namespace ast