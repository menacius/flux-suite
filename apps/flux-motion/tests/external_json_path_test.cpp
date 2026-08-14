#include "external-json-path.h"

#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

class TestJson {
public:
    enum class Kind { Object, Array, Integer, Boolean, String };

    static TestJson object(std::map<std::string, TestJson> values)
    {
        TestJson node(Kind::Object);
        node.object_ = std::move(values);
        return node;
    }
    static TestJson array(std::vector<TestJson> values)
    {
        TestJson node(Kind::Array);
        node.array_ = std::move(values);
        return node;
    }
    static TestJson integer(std::int64_t value)
    {
        TestJson node(Kind::Integer);
        node.integer_ = value;
        return node;
    }
    static TestJson boolean(bool value)
    {
        TestJson node(Kind::Boolean);
        node.boolean_ = value;
        return node;
    }
    static TestJson string(std::string value)
    {
        TestJson node(Kind::String);
        node.string_ = std::move(value);
        return node;
    }

    bool is_array() const { return kind_ == Kind::Array; }
    bool is_object() const { return kind_ == Kind::Object; }
    bool is_number_integer() const { return kind_ == Kind::Integer; }
    std::size_t size() const { return array_.size(); }
    const TestJson &operator[](std::size_t index) const { return array_.at(index); }
    bool contains(const std::string &key) const
    {
        return object_.find(key) != object_.end();
    }
    const TestJson &at(const std::string &key) const { return object_.at(key); }
    std::int64_t integer_value() const { return integer_; }

private:
    explicit TestJson(Kind kind) : kind_(kind) {}

    Kind kind_;
    std::map<std::string, TestJson> object_;
    std::vector<TestJson> array_;
    std::int64_t integer_ = 0;
    bool boolean_ = false;
    std::string string_;
};

bool expect(bool condition, const char *message)
{
    if (condition)
        return true;
    std::cerr << "external JSON path failure: " << message << '\n';
    return false;
}

} // namespace

int main()
{
    using fxm::external_data::JsonPathToken;
    using fxm::external_data::parse_json_path;
    using fxm::external_data::resolve_json_path;

    bool ok = true;
    const TestJson document = TestJson::object({
        {"match", TestJson::object({
            {"teams", TestJson::array({
                TestJson::object({{"name", TestJson::string("Home")},
                                  {"score", TestJson::integer(3)}}),
                TestJson::object({{"name", TestJson::string("Away")},
                                  {"score", TestJson::integer(2)}})
            })}
        })},
        {"active", TestJson::boolean(true)}
    });

    std::string error;
    const TestJson *score = resolve_json_path(
        document, "match.teams[1].score", &error);
    ok &= expect(score && score->is_number_integer() &&
                     score->integer_value() == 2,
                 "nested object/array field resolves");

    const TestJson *root = resolve_json_path(document, "", &error);
    ok &= expect(root == &document, "empty path resolves the root value");

    std::vector<JsonPathToken> tokens;
    ok &= expect(parse_json_path("match.teams[ 0 ].name", tokens, &error) &&
                     tokens.size() == 4 && tokens[2].is_index &&
                     tokens[2].index == 0,
                 "whitespace inside non-negative array indexes is accepted");

    error.clear();
    ok &= expect(resolve_json_path(document, "match.teams[4].score", &error) ==
                     nullptr && !error.empty(),
                 "out-of-range index fails with a diagnostic");
    error.clear();
    ok &= expect(!parse_json_path("match.teams[-1]", tokens, &error) &&
                     !error.empty(),
                 "negative indexes are rejected");
    error.clear();
    ok &= expect(!parse_json_path("match.teams[", tokens, &error) &&
                     !error.empty(),
                 "unclosed indexes are rejected");
    error.clear();
    ok &= expect(resolve_json_path(document, "active.value", &error) == nullptr &&
                     !error.empty(),
                 "traversal through a scalar is rejected");

    if (ok)
        std::cout << "external JSON path test passed\n";
    return ok ? 0 : 1;
}
