#include "description_formula.hpp"
#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace d2x {
// D2MOO Calc / SkillsTbls / SKILLS_EvaluateSkillDescFormula are the vocabulary
// evidence (MIT, The Phrozen Keep; docs/licenses/D2MOO.txt). Independent AST;
// no binary bytecode, DLL calls or gameplay evaluator is imported here.
struct DescriptionExpression {
    enum class Kind { Invalid, Constant, Reference, Unary, Binary, Conditional, Minimum, Maximum };
    Kind kind = Kind::Invalid;
    int32_t constant{};
    std::string operation;
    DescriptionReference reference;
    std::vector<DescriptionFormula> children;
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
        while (position_ < source_.size() && std::isspace(static_cast<unsigned char>(source_[position_]))) ++position_;
    }
    bool consume(std::string_view token) {
        whitespace();
        if (!source_.substr(position_).starts_with(token)) return false;
        position_ += token.size(); return true;
    }
    void require(std::string_view token) {
        if (!consume(token)) throw std::invalid_argument("Invalid SkillDesc expression");
    }
    std::string identifier() {
        whitespace(); const auto start = position_;
        while (position_ < source_.size() && (std::isalnum(static_cast<unsigned char>(source_[position_])) || source_[position_] == '_')) ++position_;
        if (start == position_) throw std::invalid_argument("Missing SkillDesc identifier");
        return lower(source_.substr(start, position_ - start));
    }
    DescriptionFormula node(DescriptionExpression::Kind kind, std::string operation,
                            std::vector<DescriptionFormula> children) {
        auto result = std::make_shared<DescriptionExpression>(); result->kind = kind;
        result->operation = std::move(operation); result->children = std::move(children); return result;
    }
    DescriptionFormula primary() {
        if (++depth_ > 64) throw std::invalid_argument("Nested SkillDesc expression");
        DescriptionFormula result;
        whitespace();
        if (consume("(")) { result = conditional(); require(")"); }
        else if (consume("-")) result = node(DescriptionExpression::Kind::Unary,"-",{primary()});
        else if (consume("+")) result = node(DescriptionExpression::Kind::Unary,"+",{primary()});
        else if (consume("!")) result = node(DescriptionExpression::Kind::Unary,"!",{primary()});
        else if (position_ < source_.size() && std::isdigit(static_cast<unsigned char>(source_[position_]))) {
            int32_t number{};
            const auto parsed = std::from_chars(source_.data()+position_,source_.data()+source_.size(),number);
            if (parsed.ec != std::errc{}) throw std::invalid_argument("Invalid SkillDesc number");
            position_ = size_t(parsed.ptr-source_.data());
            auto value = std::make_shared<DescriptionExpression>(); value->kind=DescriptionExpression::Kind::Constant;
            value->constant=number; result=std::move(value);
        } else {
            auto name = identifier();
            if (name == "min" || name == "max") {
                require("("); auto first = conditional(); require(","); auto second = conditional(); require(")");
                result = node(name == "min" ? DescriptionExpression::Kind::Minimum : DescriptionExpression::Kind::Maximum,
                              {},{first,second});
            } else {
                auto value = std::make_shared<DescriptionExpression>(); value->kind=DescriptionExpression::Kind::Reference;
                auto &reference = value->reference;
                if (name == "skill" || name == "miss" || name == "stat" || name == "sklvl") {
                    reference.kind = name == "skill" ? DescriptionReference::Kind::Skill :
                        name == "miss" ? DescriptionReference::Kind::Missile :
                        name == "stat" ? DescriptionReference::Kind::Stat : DescriptionReference::Kind::SkillAtLevel;
                    require("("); require("'"); const auto start=position_;
                    while (position_ < source_.size() && source_[position_] != '\'') ++position_;
                    if (position_ == source_.size()) throw std::invalid_argument("Unclosed SkillDesc reference");
                    reference.name=lower(source_.substr(start,position_-start)); require("'"); require(".");
                    reference.parameter=identifier();
                    if (reference.kind == DescriptionReference::Kind::SkillAtLevel) {
                        reference.levelParameter=reference.parameter; require("."); reference.parameter=identifier();
                    }
                    require(")");
                } else reference.parameter=std::move(name);
                result=std::move(value);
            }
        }
        --depth_; return result;
    }
    DescriptionFormula binary(unsigned tier) {
        if (tier == 6) return primary();
        auto result = binary(tier+1);
        static const std::vector<std::vector<std::string_view>> operators{
            {"||"},{"&&"},{"==","!="},{"<=",">=","<",">"},{"+","-"},{"*","/","%"}};
        for (;;) {
            bool found=false;
            for (const auto operation : operators[tier]) if (consume(operation)) {
                auto right=binary(tier+1);
                result=node(DescriptionExpression::Kind::Binary,std::string(operation),{result,right}); found=true; break;
            }
            if (!found) return result;
        }
    }
    DescriptionFormula conditional() {
        auto result=binary(0);
        if (consume("?")) {
            auto yes=conditional(); require(":"); auto no=conditional();
            result=node(DescriptionExpression::Kind::Conditional,{}, {result,yes,no});
        }
        return result;
    }
  public:
    explicit Parser(std::string_view source):source_(source) {}
    DescriptionFormula parse() {
        whitespace();
        if (position_==source_.size()) { auto result=std::make_shared<DescriptionExpression>(); result->kind=DescriptionExpression::Kind::Constant; return result; }
        auto result=conditional(); whitespace();
        if (position_!=source_.size()) throw std::invalid_argument("Trailing SkillDesc expression");
        return result;
    }
};
DescriptionNumber checked(int64_t number) {
    if (number < std::numeric_limits<int32_t>::min() || number > std::numeric_limits<int32_t>::max()) return {};
    return int32_t(number);
}
DescriptionNumber evaluate(const DescriptionFormula &formula, const DescriptionLookup &lookup, unsigned depth) {
    if (!formula || depth>64) return {};
    using Kind=DescriptionExpression::Kind;
    const auto &node=*formula;
    if (node.kind==Kind::Constant) return node.constant;
    if (node.kind==Kind::Reference) return lookup(node.reference);
    if (node.kind==Kind::Invalid) return {};
    auto left=evaluate(node.children[0],lookup,depth+1);
    if (!left) return {};
    if (node.kind==Kind::Conditional) return evaluate(node.children[*left ? 1:2],lookup,depth+1);
    if (node.kind==Kind::Unary) {
        if (node.operation=="-") return checked(-int64_t(*left));
        if (node.operation=="!") return int32_t(!*left);
        return left;
    }
    if (node.operation=="&&" && !*left) return 0;
    if (node.operation=="||" && *left) return 1;
    auto right=evaluate(node.children[1],lookup,depth+1);
    if (!right) return {};
    const int64_t first=*left, second=*right;
    if (node.kind==Kind::Minimum) return std::min(*left,*right);
    if (node.kind==Kind::Maximum) return std::max(*left,*right);
    if (node.operation=="+") return checked(first+second);
    if (node.operation=="-") return checked(first-second);
    if (node.operation=="*") return checked(first*second);
    if (node.operation=="/") return second ? checked(first/second):DescriptionNumber{};
    if (node.operation=="%") return second ? checked(first%second):DescriptionNumber{};
    if (node.operation=="==") return int32_t(first==second);
    if (node.operation=="!=") return int32_t(first!=second);
    if (node.operation=="<=") return int32_t(first<=second);
    if (node.operation==">=") return int32_t(first>=second);
    if (node.operation=="<") return int32_t(first<second);
    if (node.operation==">") return int32_t(first>second);
    if (node.operation=="&&") return int32_t(first && second);
    if (node.operation=="||") return int32_t(first || second);
    return {};
}
} // namespace
DescriptionFormula compileDescriptionFormula(std::string_view source) {
    if (source.size()>=2 && source.front()=='"' && source.back()=='"') source=source.substr(1,source.size()-2);
    if (source.size()>4096) return std::make_shared<DescriptionExpression>();
    if (source.empty()) {
        static const auto zero=Parser(std::string_view{}).parse();
        return zero;
    }
    try { return Parser(source).parse(); }
    catch (const std::invalid_argument &) { return std::make_shared<DescriptionExpression>(); }
}
DescriptionNumber evaluateDescriptionFormula(const DescriptionFormula &formula, const DescriptionLookup &lookup) {
    return evaluate(formula,lookup,0);
}
} // namespace d2x
