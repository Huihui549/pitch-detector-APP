// 跨语言对拍的真值读取（JSON 版，供单测使用）
//
// 为什么单测读 JSON 而对拍读二进制：
//   对拍要求逐帧数值相对差 ≤ 1e-6，JSON 序列化会丢浮点末位（坑 A10），故对拍必须用 float64 原始字节。
//   单测断言的是"阈值级"事实（某频率应判为某音名、偏差应小于某值），JSON 的 17 位有效数字足够，
//   且 JSON 便于人直接阅读与 diff。
//
// 解析范围**刻意收窄**：只支持本工具生成的真值文件——嵌套对象、字符串、浮点数、整数。
// 不实现数组、转义序列、\u 等用不到的特性（R8：不为假设的未来需求写代码）。
// 遇到不支持的结构必须报错，不得静默跳过（坑 A7/A9 的同一原则）。

#pragma once

#include <map>
#include <string>
#include <vector>

namespace pitch {
namespace tools {

/// 极简 JSON 值：本工具的真值只有对象/数组/字符串/数值四种。
class JsonValue {
public:
    enum class Kind { Null, Number, String, Array, Object };

    Kind kind = Kind::Null;
    double number = 0.0;
    std::string text;
    std::vector<JsonValue> items;                  ///< Kind::Array
    std::map<std::string, JsonValue> fields;       ///< Kind::Object（保持稳定的字典序）

    bool isNull() const { return kind == Kind::Null; }
    bool isNumber() const { return kind == Kind::Number; }
    bool isString() const { return kind == Kind::String; }
    bool isArray() const { return kind == Kind::Array; }
    bool isObject() const { return kind == Kind::Object; }

    /// 取字段；不存在时返回静态空值（避免调用方到处判空）。
    const JsonValue& operator[](const std::string& key) const;
    /// 取数组元素；越界返回静态空值。
    const JsonValue& at(std::size_t index) const;
    std::size_t size() const;
};

/// 解析 JSON 文本。失败时返回 false 并填 error。
bool parseJson(const std::string& text, JsonValue& out, std::string& error);

/// 从文件读取并解析 JSON。失败时返回 false 并填 error。
bool parseJsonFile(const std::string& path, JsonValue& out, std::string& error);

} // namespace tools
} // namespace pitch
