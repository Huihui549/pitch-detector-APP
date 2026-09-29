// 跨语言对拍的真值读取（JSON 版）—— 实现
//
// 手写递归下降解析器。存在的理由：单测需要读取由 Node 侧生成的真值（reference.json），
// 而本项目禁止引入第三方库（AGENTS.md 硬约定：不引入任何外部依赖）。
// 解析范围刻意收窄，见头文件说明。

#include "json-reader.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace pitch {
namespace tools {

namespace {

/// 解析器：持有原文本与游标。
class Parser {
public:
    Parser(const std::string& text, std::string& error) : m_text(text), m_error(error) {}

    bool parse(JsonValue& out) {
        skipWhitespace();
        if (!parseValue(out)) {
            return false;
        }
        skipWhitespace();
        if (m_pos != m_text.size()) {
            return fail("根值之后仍有未解析内容");
        }
        return true;
    }

private:
    const std::string& m_text;
    std::string& m_error;
    std::size_t m_pos = 0;

    bool fail(const std::string& message) {
        if (m_error.empty()) {
            std::ostringstream os;
            os << "JSON 解析失败（位置 " << m_pos << "）：" << message;
            m_error = os.str();
        }
        return false;
    }

    void skipWhitespace() {
        while (m_pos < m_text.size()) {
            const unsigned char c = static_cast<unsigned char>(m_text[m_pos]);
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++m_pos;
            } else {
                break;
            }
        }
    }

    bool literal(const char* word) {
        const std::size_t len = std::char_traits<char>::length(word);
        if (m_text.compare(m_pos, len, word) != 0) {
            return false;
        }
        m_pos += len;
        return true;
    }

    bool parseValue(JsonValue& out) {
        skipWhitespace();
        if (m_pos >= m_text.size()) {
            return fail("输入意外结束");
        }
        const char c = m_text[m_pos];
        if (c == '{') {
            return parseObject(out);
        }
        if (c == '[') {
            return parseArray(out);
        }
        if (c == '"') {
            out.kind = JsonValue::Kind::String;
            return parseString(out.text);
        }
        if (c == 't') {
            if (!literal("true")) {
                return fail("期望 true");
            }
            out.kind = JsonValue::Kind::Number;
            out.number = 1.0;
            return true;
        }
        if (c == 'f') {
            if (!literal("false")) {
                return fail("期望 false");
            }
            out.kind = JsonValue::Kind::Number;
            out.number = 0.0;
            return true;
        }
        if (c == 'n') {
            if (!literal("null")) {
                return fail("期望 null");
            }
            out.kind = JsonValue::Kind::Null;
            return true;
        }
        return parseNumber(out);
    }

    bool parseObject(JsonValue& out) {
        out.kind = JsonValue::Kind::Object;
        ++m_pos;   // '{'
        skipWhitespace();
        if (m_pos < m_text.size() && m_text[m_pos] == '}') {
            ++m_pos;
            return true;
        }
        while (true) {
            skipWhitespace();
            if (m_pos >= m_text.size() || m_text[m_pos] != '"') {
                return fail("对象键必须是字符串");
            }
            std::string key;
            if (!parseString(key)) {
                return false;
            }
            skipWhitespace();
            if (m_pos >= m_text.size() || m_text[m_pos] != ':') {
                return fail("对象键之后缺少冒号");
            }
            ++m_pos;
            JsonValue value;
            if (!parseValue(value)) {
                return false;
            }
            out.fields[key] = value;
            skipWhitespace();
            if (m_pos < m_text.size() && m_text[m_pos] == ',') {
                ++m_pos;
                continue;
            }
            if (m_pos < m_text.size() && m_text[m_pos] == '}') {
                ++m_pos;
                return true;
            }
            return fail("对象内缺少逗号或右花括号");
        }
    }

    bool parseArray(JsonValue& out) {
        out.kind = JsonValue::Kind::Array;
        ++m_pos;   // '['
        skipWhitespace();
        if (m_pos < m_text.size() && m_text[m_pos] == ']') {
            ++m_pos;
            return true;
        }
        while (true) {
            JsonValue value;
            if (!parseValue(value)) {
                return false;
            }
            out.items.push_back(value);
            skipWhitespace();
            if (m_pos < m_text.size() && m_text[m_pos] == ',') {
                ++m_pos;
                continue;
            }
            if (m_pos < m_text.size() && m_text[m_pos] == ']') {
                ++m_pos;
                return true;
            }
            return fail("数组内缺少逗号或右方括号");
        }
    }

    bool parseString(std::string& out) {
        ++m_pos;   // 开引号
        out.clear();
        while (m_pos < m_text.size()) {
            const char c = m_text[m_pos];
            if (c == '"') {
                ++m_pos;
                return true;
            }
            if (c == '\\') {
                // 只支持真值文件里实际会出现的换行/引号/反斜杠转义；其余明确报错而不是猜
                if (m_pos + 1 >= m_text.size()) {
                    return fail("转义序列不完整");
                }
                const char e = m_text[m_pos + 1];
                if (e == 'n') {
                    out.push_back('\n');
                } else if (e == 'r') {
                    out.push_back('\r');
                } else if (e == 't') {
                    out.push_back('\t');
                } else if (e == '"' || e == '\\' || e == '/') {
                    out.push_back(e);
                } else {
                    return fail(std::string("不支持的转义字符：\\") + e);
                }
                m_pos += 2;
                continue;
            }
            out.push_back(c);
            ++m_pos;
        }
        return fail("字符串未闭合");
    }

    bool parseNumber(JsonValue& out) {
        const std::size_t start = m_pos;
        if (m_pos < m_text.size() && (m_text[m_pos] == '-' || m_text[m_pos] == '+')) {
            ++m_pos;
        }
        bool sawDigit = false;
        while (m_pos < m_text.size()) {
            const char c = m_text[m_pos];
            if ((c >= '0' && c <= '9')) {
                sawDigit = true;
                ++m_pos;
            } else if (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
                ++m_pos;
            } else {
                break;
            }
        }
        if (!sawDigit) {
            return fail("不是合法数值");
        }
        const std::string token = m_text.substr(start, m_pos - start);
        out.kind = JsonValue::Kind::Number;
        out.number = std::strtod(token.c_str(), nullptr);
        return true;
    }
};

const JsonValue& emptyValue() {
    static const JsonValue empty;
    return empty;
}

} // namespace

const JsonValue& JsonValue::operator[](const std::string& key) const {
    const auto it = fields.find(key);
    if (it == fields.end()) {
        return emptyValue();
    }
    return it->second;
}

const JsonValue& JsonValue::at(std::size_t index) const {
    if (index >= items.size()) {
        return emptyValue();
    }
    return items[index];
}

std::size_t JsonValue::size() const {
    if (kind == Kind::Array) {
        return items.size();
    }
    if (kind == Kind::Object) {
        return fields.size();
    }
    return 0;
}

bool parseJson(const std::string& text, JsonValue& out, std::string& error) {
    error.clear();
    Parser parser(text, error);
    return parser.parse(out);
}

bool parseJsonFile(const std::string& path, JsonValue& out, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "无法打开文件：" + path;
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return parseJson(buffer.str(), out, error);
}

} // namespace tools
} // namespace pitch
