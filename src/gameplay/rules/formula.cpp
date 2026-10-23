#include "formula.hpp"
#include "arithmetic.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <span>
#include <utility>
#include <vector>

namespace d2x {
struct RuleExpression {
    enum class Kind { Invalid, Constant, Reference, Unary, Binary, Conditional, Minimum, Maximum, Random };
    Kind kind = Kind::Invalid;
    int32_t constant{};
    std::string operation;
    RuleReference reference;
    std::vector<RuleFormula> children;
};
namespace {
std::string lower(std::string_view text) {
    std::string result(text);
    for (auto &letter : result) letter = char(std::tolower(static_cast<unsigned char>(letter)));
    return result;
}
class Parser {
    std::string_view source_;
    size_t position_{};
    unsigned depth_{};
    void whitespace() {
        while (position_ < source_.size() &&
               (std::isspace(static_cast<unsigned char>(source_[position_])) || source_[position_] == '"')) ++position_;
    }
    bool consume(std::string_view token) {
        whitespace();
        if (!source_.substr(position_).starts_with(token)) return false;
        position_ += token.size(); return true;
    }
    void require(std::string_view token) {
        if (!consume(token)) throw std::invalid_argument("Invalid rule expression");
    }
    std::string identifier() {
        whitespace(); const auto start = position_;
        while (position_ < source_.size() && (std::isalnum(static_cast<unsigned char>(source_[position_])) || source_[position_] == '_')) ++position_;
        if (start == position_) throw std::invalid_argument("Missing rule identifier");
        return lower(source_.substr(start, position_ - start));
    }
    RuleFormula node(RuleExpression::Kind kind, std::string operation,
                     std::vector<RuleFormula> children) {
        auto result = std::make_shared<RuleExpression>(); result->kind = kind;
        result->operation = std::move(operation); result->children = std::move(children); return result;
    }
    RuleFormula primary() {
        if (++depth_ > 64) throw std::invalid_argument("Nested rule expression");
        RuleFormula result;
        whitespace();
        if (consume("(")) { result = expression(); require(")"); }
        else if (position_ < source_.size() && std::isdigit(static_cast<unsigned char>(source_[position_]))) {
            uint32_t number{};
            const auto parsed = std::from_chars(source_.data()+position_,source_.data()+source_.size(),number);
            if (parsed.ec != std::errc{}) throw std::invalid_argument("Invalid rule number");
            position_ = size_t(parsed.ptr-source_.data());
            auto value = std::make_shared<RuleExpression>(); value->kind=RuleExpression::Kind::Constant;
            value->constant=nativeNarrow(number); result=std::move(value);
        } else {
            auto name = identifier();
            if (name == "min" || name == "max" || name == "rand") {
                require("("); auto first = expression(); require(","); auto second = expression(); require(")");
                result = node(name == "min" ? RuleExpression::Kind::Minimum : name == "max" ? RuleExpression::Kind::Maximum : RuleExpression::Kind::Random,
                              {},{first,second});
            } else {
                auto value = std::make_shared<RuleExpression>(); value->kind=RuleExpression::Kind::Reference;
                auto &reference = value->reference;
                if (name == "skill" || name == "miss" || name == "stat" || name == "sklvl") {
                    reference.kind = name == "skill" ? RuleReference::Kind::Skill :
                        name == "miss" ? RuleReference::Kind::Missile :
                        name == "stat" ? RuleReference::Kind::Stat : RuleReference::Kind::SkillAtLevel;
                    require("("); require("'"); const auto start=position_;
                    while (position_ < source_.size() && source_[position_] != '\'') ++position_;
                    if (position_ == source_.size()) throw std::invalid_argument("Unclosed rule reference");
                    reference.name=lower(source_.substr(start,position_-start)); require("'"); require(".");
                    reference.parameter=identifier();
                    if (reference.kind == RuleReference::Kind::SkillAtLevel) {
                        reference.levelParameter=reference.parameter; require("."); reference.parameter=identifier();
                    }
                    require(")");
                } else reference.parameter=std::move(name);
                result=std::move(value);
            }
        }
        --depth_; return result;
    }
    RuleFormula expression() {
        // Fog::DATATABLS_CheckPrecendence / HandleNewOp. Comparison operators
        // share one precedence; power is left associative; ternary has native
        // precedence 22. Do not replace this with C++ conditional precedence.
        struct Operation {std::string token;unsigned precedence{},arity{};};
        std::vector<RuleFormula> values;
        std::vector<Operation> pending;
        bool hasValue=false;
        unsigned questions=0,colons=0;
        const auto emit=[&]() {
            const auto op=pending.back();pending.pop_back();
            if(values.size()<op.arity) throw std::invalid_argument("Missing rule operands");
            std::vector<RuleFormula> children(values.end()-op.arity,values.end());
            values.resize(values.size()-op.arity);
            values.push_back(node(op.arity==3?RuleExpression::Kind::Conditional:op.arity==1?RuleExpression::Kind::Unary:RuleExpression::Kind::Binary,op.token,std::move(children)));
        };
        const auto push=[&](Operation op) {
            while(!pending.empty() && pending.back().precedence>=op.precedence) emit();
            if(pending.size()>=64) throw std::invalid_argument("Native rule operator stack full");
            pending.push_back(std::move(op));
        };
        for(;;) {
            whitespace();
            if(position_==source_.size() || source_[position_]==')' || source_[position_]==',') break;
            if(!hasValue) {
                if(consume("-")) {push({"-",21,1});continue;}
                values.push_back(primary());hasValue=true;
                if(values.size()>64) throw std::invalid_argument("Native rule value stack full");
                continue;
            }
            if(consume(":")) {++colons;hasValue=false;continue;}
            if(consume("?")) {push({"?",22,3});++questions;hasValue=false;continue;}
            bool found=false;
            for(const auto token:{"<=",">=","==","!=","<",">","+","-","*","/","^"}) {
                if(!consume(token)) continue;
                const std::string op(token);
                const unsigned precedence=op=="^"?20:op=="*" || op=="/"?19:op=="+" || op=="-"?17:15;
                push({op,precedence,2});hasValue=false;found=true;break;
            }
            if(!found) throw std::invalid_argument("Unsupported native rule operator");
        }
        if(!hasValue || questions!=colons) throw std::invalid_argument("Incomplete native rule expression");
        while(!pending.empty()) emit();
        if(values.size()!=1) throw std::invalid_argument("Invalid native rule result stack");
        return values.front();
    }
  public:
    explicit Parser(std::string_view source):source_(source) {}
    RuleFormula parse() {
        whitespace();
        if (position_==source_.size()) { auto result=std::make_shared<RuleExpression>(); result->kind=RuleExpression::Kind::Constant; return result; }
        auto result=expression(); whitespace();
        if (position_!=source_.size()) throw std::invalid_argument("Trailing rule expression");
        return result;
    }
};
RuleNumber checked(int64_t number) {
    return nativeNarrow(number);
}
RuleNumber apply(const RuleExpression &node,std::span<const RuleNumber> arguments,const RuleRandom &random) {
    using Kind=RuleExpression::Kind;
    const auto left=arguments[0];
    if (!left) return {};
    if (node.kind==Kind::Conditional) {
        return *left ? arguments[1] : arguments[2];
    }
    if (node.kind==Kind::Unary) {
        if (node.operation=="-") return checked(-int64_t(*left));
        return left;
    }
    const auto right=arguments[1];
    if (!right) return {};
    const int64_t first=*left, second=*right;
    if (node.kind==Kind::Minimum) return std::min(*left,*right);
    if (node.kind==Kind::Maximum) return std::max(*left,*right);
    if (node.kind==Kind::Random) return random?random(*left,*right):RuleNumber{};
    if (node.operation=="+") return checked(first+second);
    if (node.operation=="-") return checked(first-second);
    if (node.operation=="*") return checked(first*second);
    if (node.operation=="/") return nativeDivide(*left, *right);
    if (node.operation=="^") {
        if (*right <= 0) return 1;
        int32_t value=1,base=*left;
        auto exponent=uint32_t(*right);
        while(exponent) {
            if(exponent&1) value=nativeMultiply(value,base);
            exponent >>= 1;
            if(exponent) base=nativeMultiply(base,base);
        }
        return value;
    }
    if (node.operation=="==") return int32_t(first==second);
    if (node.operation=="!=") return int32_t(first!=second);
    if (node.operation=="<=") return int32_t(first<=second);
    if (node.operation==">=") return int32_t(first>=second);
    if (node.operation=="<") return int32_t(first<second);
    if (node.operation==">") return int32_t(first>second);
    return {};
}
RuleNumber evaluate(const RuleFormula &formula,const RuleLookup &lookup,const RuleRandom &random) {
    if(!formula) return {};
    struct Cursor {const RuleExpression *node;size_t next{};};
    std::vector<Cursor> program{{formula.get(),0}};
    std::vector<RuleNumber> stack;
    stack.reserve(64);
    while(!program.empty()) {
        auto &cursor=program.back();const auto &node=*cursor.node;
        if(cursor.next<node.children.size()) {
            const auto *child=node.children[cursor.next++].get();
            if(!child) return {};
            program.push_back({child,0});continue;
        }
        RuleNumber value;
        using Kind=RuleExpression::Kind;
        if(node.kind==Kind::Constant) value=node.constant;
        else if(node.kind==Kind::Reference) value=lookup?lookup(node.reference):RuleNumber{};
        else if(node.kind==Kind::Invalid) return {};
        else {
            const auto count=node.children.size();
            if(count>stack.size() || !count) return {};
            value=apply(node,std::span<const RuleNumber>(stack).last(count),random);
            stack.resize(stack.size()-count);
        }
        if(stack.size()>=64) return {};
        stack.push_back(value);program.pop_back();
    }
    return stack.size()==1?stack.front():RuleNumber{};
}
}
RuleFormula compileRuleFormula(std::string_view source) {
    if (source.size()>=2 && source.front()=='"' && source.back()=='"') source=source.substr(1,source.size()-2);
    if (source.size()>4096) return std::make_shared<RuleExpression>();
    if (source.empty()) {
        static const auto zero=Parser(std::string_view{}).parse();
        return zero;
    }
    try { return Parser(source).parse(); }
    catch (const std::invalid_argument &) { return std::make_shared<RuleExpression>(); }
}
RuleNumber evaluateRuleFormula(const RuleFormula &formula, const RuleLookup &lookup,const RuleRandom &random) {
    return evaluate(formula,lookup,random);
}
RuleNumber evaluateRuleFormula(const RuleFormula &formula,const RuleLookup &lookup,uint64_t &random) {
    return evaluateRuleFormula(formula,lookup,[&](int32_t minimum,int32_t maximum)->RuleNumber {
        if(minimum>=maximum) return minimum;
        const auto range=nativeAdd(nativeSubtract(maximum,minimum),1);
        return nativeAdd(minimum,int32_t(limitedRandom(random,uint32_t(range))));
    });
}
}
