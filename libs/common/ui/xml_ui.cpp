#if defined(_WIN32)

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>

#include "xml_ui.hpp"

#include <d2d1.h>
#include <dwrite.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace ogg::ui {

namespace detail {

constexpr wchar_t kXmlUiClass[] = L"OGG.XmlUiHost";
constexpr float kDefaultGap = 16.f;
constexpr float kFieldGap = 6.f;

struct EdgeInsets {
    float top = 0.f;
    float right = 0.f;
    float bottom = 0.f;
    float left = 0.f;
};

struct StyleRule {
    std::string name;
    EdgeInsets padding{};
    EdgeInsets margin{};
    float width = -1.f;
    float height = -1.f;
    D2D1_COLOR_F color{ 0.12f, 0.12f, 0.12f, 1.f };
    D2D1_COLOR_F bg_color{ 1.f, 1.f, 1.f, 1.f };
    D2D1_COLOR_F hover_color{ 0.12f, 0.12f, 0.12f, 1.f };
    D2D1_COLOR_F hover_bg_color{ 0.29f, 0.68f, 0.94f, 1.f };
    D2D1_COLOR_F border_color{ 0.78f, 0.78f, 0.78f, 1.f };
    D2D1_COLOR_F focus_border_color{ 0.35f, 0.75f, 1.f, 1.f };
    bool has_color = false;
    bool has_bg = false;
    bool has_hover_color = false;
    bool has_hover_bg = false;
    bool has_border = false;
    bool has_focus_border = false;
    float font_size = 14.f;
    int font_weight = 400;
    float radius = 0.f;
};

enum class NodeType { Div, Text, Label, Input, Button };

struct UiNode {
    NodeType type = NodeType::Div;
    std::string class_name;
    std::string id;
    std::string text;
    std::string placeholder;
    std::string input_type;
    std::vector<std::unique_ptr<UiNode>> children;
    D2D1_RECT_F bounds{};
    int button_index = -1;
};

struct InputControl {
    std::string id;
    HWND hwnd = nullptr;
    D2D1_RECT_F bounds{};
};

struct XmlUiState {
    std::vector<StyleRule> rules;
    std::unique_ptr<UiNode> root;
    std::vector<InputControl> inputs;
    std::vector<UiNode*> buttons;
    int hovered_button = -1;
    bool mouse_tracking = false;
    bool layout_in_progress = false;
    bool paint_ready = false;
    float layout_width = 320.f;
    ID2D1Factory* factory = nullptr;
    IDWriteFactory* write_factory = nullptr;
    ID2D1HwndRenderTarget* target = nullptr;
    ID2D1SolidColorBrush* text_brush = nullptr;
    ID2D1SolidColorBrush* bg_brush = nullptr;
    ID2D1SolidColorBrush* border_brush = nullptr;
    std::unordered_map<std::string, IDWriteTextFormat*> text_formats;
};

} // namespace detail

struct detail::XmlUiImpl {
    detail::XmlUiState state{};
    XmlUiHost* owner = nullptr;
};

namespace detail {

XmlUiImpl* state_from_hwnd(HWND hwnd) {
    return reinterpret_cast<XmlUiImpl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

std::string trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(start, end - start);
}

bool parse_hex_color(const std::string& value, D2D1_COLOR_F& out) {
    if (value.size() != 7 || value[0] != '#') return false;
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    int r = (hex(value[1]) << 4) | hex(value[2]);
    int g = (hex(value[3]) << 4) | hex(value[4]);
    int b = (hex(value[5]) << 4) | hex(value[6]);
    if (r < 0 || g < 0 || b < 0) return false;
    out = D2D1::ColorF(static_cast<float>(r) / 255.f, static_cast<float>(g) / 255.f, static_cast<float>(b) / 255.f, 1.f);
    return true;
}

float parse_float(const std::string& value, float fallback) {
    if (value.empty()) return fallback;
    char* end = nullptr;
    const float v = std::strtof(value.c_str(), &end);
    if (end == value.c_str()) return fallback;
    return v;
}

EdgeInsets parse_insets(const std::string& value) {
    EdgeInsets insets{};
    std::vector<std::string> parts;
    std::string current;
    for (char c : value) {
        if (c == ',') {
            parts.push_back(trim(current));
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    parts.push_back(trim(current));
    if (parts.size() == 1) {
        const float v = parse_float(parts[0], 0.f);
        insets.top = insets.right = insets.bottom = insets.left = v;
    } else if (parts.size() == 2) {
        insets.top = insets.bottom = parse_float(parts[0], 0.f);
        insets.left = insets.right = parse_float(parts[1], 0.f);
    } else if (parts.size() == 4) {
        insets.top = parse_float(parts[0], 0.f);
        insets.right = parse_float(parts[1], 0.f);
        insets.bottom = parse_float(parts[2], 0.f);
        insets.left = parse_float(parts[3], 0.f);
    }
    return insets;
}

void apply_attr(StyleRule& rule, const std::string& key, const std::string& value) {
    if (key == "rule") {
        rule.name = value;
    } else if (key == "padding") {
        rule.padding = parse_insets(value);
    } else if (key == "margin") {
        rule.margin = parse_insets(value);
    } else if (key == "width") {
        rule.width = parse_float(value, -1.f);
    } else if (key == "height") {
        rule.height = parse_float(value, -1.f);
    } else if (key == "color") {
        if (parse_hex_color(value, rule.color)) rule.has_color = true;
    } else if (key == "bg_color") {
        if (parse_hex_color(value, rule.bg_color)) rule.has_bg = true;
    } else if (key == "hover_color") {
        if (parse_hex_color(value, rule.hover_color)) rule.has_hover_color = true;
    } else if (key == "hover_bg_color") {
        if (parse_hex_color(value, rule.hover_bg_color)) rule.has_hover_bg = true;
    } else if (key == "border_color") {
        if (parse_hex_color(value, rule.border_color)) rule.has_border = true;
    } else if (key == "focus_border_color") {
        if (parse_hex_color(value, rule.focus_border_color)) rule.has_focus_border = true;
    } else if (key == "font_size") {
        rule.font_size = parse_float(value, rule.font_size);
    } else if (key == "font_weight") {
        rule.font_weight = static_cast<int>(parse_float(value, static_cast<float>(rule.font_weight)));
    } else if (key == "radius") {
        rule.radius = parse_float(value, 0.f);
    }
}

void apply_node_attr(UiNode& node, const std::string& key, const std::string& value) {
    if (key == "class") {
        node.class_name = value;
    } else if (key == "id") {
        node.id = value;
    } else if (key == "placeholder") {
        node.placeholder = value;
    } else if (key == "type") {
        node.input_type = value;
    }
}

NodeType tag_to_type(const std::string& tag) {
    if (tag == "Layout") return NodeType::Div;
    if (tag == "Div") return NodeType::Div;
    if (tag == "Text") return NodeType::Text;
    if (tag == "Label") return NodeType::Label;
    if (tag == "Input") return NodeType::Input;
    if (tag == "Button") return NodeType::Button;
    return NodeType::Div;
}

struct XmlToken {
    enum Kind { TagOpen, TagClose, TagSelfClose, Text, End };
    Kind kind = End;
    std::string name;
    std::unordered_map<std::string, std::string> attrs;
    std::string text;
};

class XmlLexer {
public:
    explicit XmlLexer(const char* source) : src_(source ? source : "") {}

    XmlToken next() {
        skip_ws();
        if (pos_ >= src_.size()) return { XmlToken::End };

        if (src_[pos_] == '<') {
            ++pos_;
            if (pos_ < src_.size() && src_[pos_] == '/') {
                ++pos_;
                const std::string name = read_name();
                expect('>');
                return { XmlToken::TagClose, name };
            }
            const std::string name = read_name();
            auto attrs = read_attrs();
            if (pos_ < src_.size() && src_[pos_] == '/') {
                ++pos_;
                expect('>');
                return { XmlToken::TagSelfClose, name, attrs };
            }
            expect('>');
            return { XmlToken::TagOpen, name, attrs };
        }

        std::string text;
        while (pos_ < src_.size() && src_[pos_] != '<') {
            text.push_back(src_[pos_++]);
        }
        return { XmlToken::Text, {}, {}, trim(text) };
    }

private:
    std::string src_;
    size_t pos_ = 0;

    void skip_ws() {
        while (pos_ < src_.size() && std::isspace(static_cast<unsigned char>(src_[pos_]))) ++pos_;
    }

    void expect(char c) {
        if (pos_ < src_.size() && src_[pos_] == c) ++pos_;
    }

    std::string read_name() {
        std::string name;
        while (pos_ < src_.size()) {
            const char c = src_[pos_];
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
                name.push_back(c);
                ++pos_;
            } else {
                break;
            }
        }
        return name;
    }

    std::unordered_map<std::string, std::string> read_attrs() {
        std::unordered_map<std::string, std::string> attrs;
        skip_ws();
        while (pos_ < src_.size() && src_[pos_] != '>' && src_[pos_] != '/') {
            const std::string key = read_name();
            skip_ws();
            if (pos_ < src_.size() && src_[pos_] == '=') {
                ++pos_;
                skip_ws();
                char quote = '"';
                if (pos_ < src_.size() && (src_[pos_] == '"' || src_[pos_] == '\'')) {
                    quote = src_[pos_++];
                }
                std::string value;
                while (pos_ < src_.size() && src_[pos_] != quote) {
                    value.push_back(src_[pos_++]);
                }
                if (pos_ < src_.size() && src_[pos_] == quote) ++pos_;
                attrs[key] = value;
            }
            skip_ws();
        }
        return attrs;
    }
};

const StyleRule& resolve_rule(const XmlUiState& state, const std::string& class_name) {
    static StyleRule empty{};
    for (const auto& rule : state.rules) {
        if (rule.name == class_name) return rule;
    }
    return empty;
}

bool ensure_write_factory(XmlUiState& state) {
    if (state.write_factory) return true;
    return SUCCEEDED(DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(&state.write_factory)));
}

IDWriteTextFormat* format_for_rule(XmlUiState& state, const StyleRule& rule) {
    if (!ensure_write_factory(state)) return nullptr;
    const std::string key = rule.name + "|" + std::to_string(rule.font_size) + "|" + std::to_string(rule.font_weight);
    auto it = state.text_formats.find(key);
    if (it != state.text_formats.end()) return it->second;

    IDWriteTextFormat* format = nullptr;
    const DWRITE_FONT_WEIGHT weight = rule.font_weight >= 600 ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
    if (FAILED(state.write_factory->CreateTextFormat(
            L"Segoe UI",
            nullptr,
            weight,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            rule.font_size,
            L"en-us",
            &format))) {
        return nullptr;
    }
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    state.text_formats[key] = format;
    return format;
}

float measure_text_height(XmlUiState& state, const std::wstring& text, const StyleRule& rule) {
    IDWriteTextFormat* format = format_for_rule(state, rule);
    if (!format || text.empty()) return rule.font_size * 1.2f;
    IDWriteTextLayout* layout = nullptr;
    if (FAILED(state.write_factory->CreateTextLayout(
            text.c_str(),
            static_cast<UINT32>(text.size()),
            format,
            280.f,
            1000.f,
            &layout))) {
        return rule.font_size * 1.2f;
    }
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    layout->Release();
    return metrics.height;
}

std::wstring to_wide(const std::string& text) {
    if (text.empty()) return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    if (len <= 0) return {};
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), len);
    return out;
}

float measure_node_height(XmlUiState& state, const UiNode& node, float content_width);

float measure_children_height(XmlUiState& state, const UiNode& node, float content_width) {
    float total = 0.f;
    bool first = true;
    for (const auto& child : node.children) {
        const StyleRule& child_rule = resolve_rule(state, child->class_name);
        if (!first) {
            total += std::max(child_rule.margin.top, kDefaultGap);
        }
        total += child_rule.margin.top;
        total += measure_node_height(state, *child, content_width - child_rule.margin.left - child_rule.margin.right);
        total += child_rule.margin.bottom;
        first = false;
    }
    return total;
}

float measure_node_height(XmlUiState& state, const UiNode& node, float content_width) {
    const StyleRule& rule = resolve_rule(state, node.class_name);
    switch (node.type) {
    case NodeType::Div:
        return rule.padding.top + measure_children_height(state, node, content_width - rule.padding.left - rule.padding.right) + rule.padding.bottom;
    case NodeType::Text:
    case NodeType::Label:
        return rule.padding.top + measure_text_height(state, to_wide(node.text), rule) + rule.padding.bottom;
    case NodeType::Input:
        return rule.padding.top + (rule.height > 0.f ? rule.height : 36.f) + rule.padding.bottom;
    case NodeType::Button:
        return rule.padding.top + (rule.height > 0.f ? rule.height : 38.f) + rule.padding.bottom;
    }
    return 0.f;
}

void layout_node(XmlUiState& state, UiNode& node, float x, float y, float width) {
    const StyleRule& rule = resolve_rule(state, node.class_name);
    const float local_x = x + rule.margin.left;
    const float local_y = y + rule.margin.top;
    const float inner_w = width - rule.margin.left - rule.margin.right;
    const float inner_h = measure_node_height(state, node, inner_w) - rule.margin.top - rule.margin.bottom;

    node.bounds = D2D1::RectF(local_x, local_y, local_x + inner_w, local_y + inner_h);

    if (node.type == NodeType::Input || node.type == NodeType::Button) {
        const float h = rule.height > 0.f ? rule.height : (node.type == NodeType::Input ? 36.f : 38.f);
        node.bounds = D2D1::RectF(
            local_x + rule.padding.left,
            local_y + rule.padding.top,
            local_x + inner_w - rule.padding.right,
            local_y + rule.padding.top + h
        );
    }

    if (node.type == NodeType::Div) {
        float child_y = local_y + rule.padding.top;
        bool first = true;
        for (auto& child : node.children) {
            const StyleRule& child_rule = resolve_rule(state, child->class_name);
            if (!first) child_y += std::max(child_rule.margin.top, kDefaultGap);
            child_y += child_rule.margin.top;
            layout_node(state, *child, local_x + rule.padding.left, child_y, inner_w - rule.padding.left - rule.padding.right);
            child_y += child->bounds.bottom - child->bounds.top + child_rule.margin.bottom;
            first = false;
        }
    }
}

void layout_tree(XmlUiState& state) {
    if (!state.root) return;
    layout_node(state, *state.root, 0.f, 0.f, state.layout_width);
}

void destroy_render(XmlUiState& state) {
    for (auto& pair : state.text_formats) {
        if (pair.second) pair.second->Release();
    }
    state.text_formats.clear();
    if (state.text_brush) { state.text_brush->Release(); state.text_brush = nullptr; }
    if (state.bg_brush) { state.bg_brush->Release(); state.bg_brush = nullptr; }
    if (state.border_brush) { state.border_brush->Release(); state.border_brush = nullptr; }
    if (state.target) { state.target->Release(); state.target = nullptr; }
}

bool ensure_render(XmlUiState& state, HWND hwnd) {
    if (!state.factory) {
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &state.factory))) return false;
    }
    if (!ensure_write_factory(state)) return false;
    if (!state.target) {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        const UINT width = static_cast<UINT32>(rc.right - rc.left);
        const UINT height = static_cast<UINT32>(rc.bottom - rc.top);
        if (width == 0 || height == 0) return false;
        const D2D1_SIZE_U size{ width, height };
        if (FAILED(state.factory->CreateHwndRenderTarget(
                D2D1::RenderTargetProperties(),
                D2D1::HwndRenderTargetProperties(hwnd, size),
                &state.target))) {
            return false;
        }
    }
    return true;
}

ID2D1SolidColorBrush* make_brush(ID2D1HwndRenderTarget* target, const D2D1_COLOR_F& color) {
    if (!target) return nullptr;
    ID2D1SolidColorBrush* brush = nullptr;
    target->CreateSolidColorBrush(color, &brush);
    return brush;
}

void release_brush(ID2D1SolidColorBrush* brush) {
    if (brush) brush->Release();
}

void draw_rounded_rect(ID2D1HwndRenderTarget* target, const D2D1_RECT_F& rect, float radius, ID2D1Brush* brush) {
    if (radius > 0.f) {
        D2D1_ROUNDED_RECT round{ rect, radius, radius };
        target->FillRoundedRectangle(round, brush);
    } else {
        target->FillRectangle(rect, brush);
    }
}

void paint_node(XmlUiState& state, const UiNode& node, int hovered_button) {
    const StyleRule& rule = resolve_rule(state, node.class_name);
    auto* target = state.target;

    if (node.type == NodeType::Div && rule.has_bg) {
        ID2D1SolidColorBrush* brush = make_brush(target, rule.bg_color);
        if (brush) {
            draw_rounded_rect(target, node.bounds, rule.radius, brush);
            release_brush(brush);
        }
    }

    if (node.type == NodeType::Text || node.type == NodeType::Label) {
        IDWriteTextFormat* format = format_for_rule(state, rule);
        if (format) {
            const D2D1_COLOR_F color = rule.has_color ? rule.color : D2D1::ColorF(0.12f, 0.12f, 0.12f, 1.f);
            ID2D1SolidColorBrush* brush = make_brush(target, color);
            if (brush) {
                const std::wstring text = to_wide(node.text);
                D2D1_RECT_F text_rect{
                    node.bounds.left + rule.padding.left,
                    node.bounds.top + rule.padding.top,
                    node.bounds.right - rule.padding.right,
                    node.bounds.bottom - rule.padding.bottom,
                };
                target->DrawText(text.c_str(), static_cast<UINT32>(text.size()), format, text_rect, brush);
                release_brush(brush);
            }
        }
    }

    if (node.type == NodeType::Button) {
        const bool hovered = node.button_index >= 0 && node.button_index == hovered_button;
        const D2D1_COLOR_F bg = hovered && rule.has_hover_bg ? rule.hover_bg_color : (rule.has_bg ? rule.bg_color : D2D1::ColorF(0.35f, 0.75f, 1.f, 1.f));
        const D2D1_COLOR_F fg = hovered && rule.has_hover_color ? rule.hover_color : (rule.has_color ? rule.color : D2D1::ColorF(0.05f, 0.07f, 0.09f, 1.f));
        ID2D1SolidColorBrush* bg_brush = make_brush(target, bg);
        if (bg_brush) {
            draw_rounded_rect(target, node.bounds, rule.radius > 0.f ? rule.radius : 6.f, bg_brush);
            release_brush(bg_brush);
        }

        IDWriteTextFormat* format = format_for_rule(state, rule);
        if (format) {
            format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            ID2D1SolidColorBrush* text_brush = make_brush(target, fg);
            if (text_brush) {
                const std::wstring text = to_wide(node.text);
                target->DrawText(text.c_str(), static_cast<UINT32>(text.size()), format, node.bounds, text_brush);
                release_brush(text_brush);
            }
            format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        }
    }

    for (const auto& child : node.children) {
        paint_node(state, *child, hovered_button);
    }
}

void destroy_inputs(XmlUiState& state) {
    for (auto& input : state.inputs) {
        if (input.hwnd) DestroyWindow(input.hwnd);
    }
    state.inputs.clear();
}

void create_inputs(XmlUiState& state, HWND parent) {
    destroy_inputs(state);
    if (!state.root) return;

    std::vector<UiNode*> stack;
    stack.push_back(state.root.get());
    while (!stack.empty()) {
        UiNode& node = *stack.back();
        stack.pop_back();

        if (node.type == NodeType::Input) {
            const StyleRule& rule = resolve_rule(state, node.class_name);
            InputControl control{};
            control.id = node.id;
            control.bounds = node.bounds;
            const int x = static_cast<int>(node.bounds.left);
            const int y = static_cast<int>(node.bounds.top);
            const int w = static_cast<int>(node.bounds.right - node.bounds.left);
            const int h = static_cast<int>(node.bounds.bottom - node.bounds.top);
            if (w > 0 && h > 0) {
                DWORD style = WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL;
                if (node.input_type == "password") style |= ES_PASSWORD;
                control.hwnd = CreateWindowExW(
                    WS_EX_CLIENTEDGE,
                    L"EDIT",
                    L"",
                    style,
                    x,
                    y,
                    w,
                    h,
                    parent,
                    nullptr,
                    GetModuleHandleW(nullptr),
                    nullptr
                );
                if (control.hwnd) {
                    HFONT font = CreateFontW(
                        -static_cast<int>(rule.font_size),
                        0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
                    );
                    SendMessageW(control.hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                }
                state.inputs.push_back(control);
            }
        }
        if (node.type == NodeType::Button) {
            node.button_index = static_cast<int>(state.buttons.size());
            state.buttons.push_back(&node);
        }
        for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) {
            stack.push_back(it->get());
        }
    }
}

bool point_in_bounds(POINT pt, const D2D1_RECT_F& rect) {
    return static_cast<float>(pt.x) >= rect.left &&
           static_cast<float>(pt.x) <= rect.right &&
           static_cast<float>(pt.y) >= rect.top &&
           static_cast<float>(pt.y) <= rect.bottom;
}

int hit_test_button(const XmlUiState& state, POINT pt) {
    for (const UiNode* button : state.buttons) {
        if (button && point_in_bounds(pt, button->bounds)) return button->button_index;
    }
    return -1;
}

bool hit_test_input(const XmlUiState& state, POINT pt) {
    for (const auto& input : state.inputs) {
        if (point_in_bounds(pt, input.bounds)) return true;
    }
    return false;
}

bool parse_xml(XmlUiState& state, const char* xml) {
    XmlLexer lexer(xml);
    struct Frame { std::string name; UiNode* node = nullptr; };
    std::vector<Frame> stack;

    while (true) {
        XmlToken token = lexer.next();
        if (token.kind == XmlToken::End) break;

        if (token.kind == XmlToken::TagOpen) {
            if (token.name == "Class") {
                StyleRule rule{};
                for (const auto& attr : token.attrs) apply_attr(rule, attr.first, attr.second);
                if (!rule.name.empty()) state.rules.push_back(rule);
                continue;
            }

            auto node = std::make_unique<UiNode>();
            node->type = tag_to_type(token.name);
            for (const auto& attr : token.attrs) apply_node_attr(*node, attr.first, attr.second);
            if (token.name == "Layout") {
                const auto width_it = token.attrs.find("width");
                if (width_it != token.attrs.end()) {
                    state.layout_width = parse_float(width_it->second, state.layout_width);
                }
            }

            UiNode* raw = node.get();
            if (stack.empty()) {
                state.root = std::move(node);
            } else {
                stack.back().node->children.push_back(std::move(node));
            }
            stack.push_back({ token.name, raw });
        } else if (token.kind == XmlToken::TagSelfClose) {
            if (token.name == "Class") {
                StyleRule rule{};
                for (const auto& attr : token.attrs) apply_attr(rule, attr.first, attr.second);
                if (!rule.name.empty()) state.rules.push_back(rule);
                continue;
            }
            auto node = std::make_unique<UiNode>();
            node->type = tag_to_type(token.name);
            for (const auto& attr : token.attrs) apply_node_attr(*node, attr.first, attr.second);
            if (stack.empty()) {
                state.root = std::move(node);
            } else if (stack.back().node) {
                stack.back().node->children.push_back(std::move(node));
            }
        } else if (token.kind == XmlToken::Text) {
            if (!stack.empty() && stack.back().node && !token.text.empty()) {
                stack.back().node->text = token.text;
            }
        } else if (token.kind == XmlToken::TagClose) {
            if (!stack.empty() && stack.back().name == token.name) {
                stack.pop_back();
            }
        }
    }

    return state.root != nullptr;
}

} // namespace detail

XmlUiHost::~XmlUiHost() {
    destroy();
}

bool XmlUiHost::create(HWND parent, const char* xml) {
    if (!parent || !xml) return false;
    destroy();

    impl_ = new detail::XmlUiImpl{};
    impl_->owner = this;
    if (!detail::parse_xml(impl_->state, xml)) {
        delete impl_;
        impl_ = nullptr;
        return false;
    }

    WNDCLASSEXW wc{};
    if (!GetClassInfoExW(GetModuleHandleW(nullptr), detail::kXmlUiClass, &wc)) {
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = XmlUiHost::window_proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = detail::kXmlUiClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        if (!RegisterClassExW(&wc)) {
            delete impl_;
            impl_ = nullptr;
            return false;
        }
    }

    parent_ = parent;
    impl_->state.paint_ready = false;
    hwnd_ = CreateWindowExW(
        0,
        detail::kXmlUiClass,
        nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0,
        0,
        static_cast<int>(impl_->state.layout_width),
        100,
        parent,
        nullptr,
        GetModuleHandleW(nullptr),
        impl_
    );
    if (!hwnd_) {
        delete impl_;
        impl_ = nullptr;
        return false;
    }

    layout(0, 0, static_cast<int>(impl_->state.layout_width), 100);
    impl_->state.paint_ready = true;
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

void XmlUiHost::destroy() {
  if (hwnd_) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
  if (impl_) {
    detail::destroy_inputs(impl_->state);
    detail::destroy_render(impl_->state);
    if (impl_->state.factory) { impl_->state.factory->Release(); impl_->state.factory = nullptr; }
    if (impl_->state.write_factory) { impl_->state.write_factory->Release(); impl_->state.write_factory = nullptr; }
    delete impl_;
    impl_ = nullptr;
  }
  parent_ = nullptr;
}

void XmlUiHost::layout(int x, int y, int width, int height) {
    if (!hwnd_ || !impl_) return;
    if (width <= 0 || height <= 0) return;
    impl_->state.layout_in_progress = true;
    SetWindowPos(hwnd_, HWND_TOP, x, y, width, height, SWP_NOACTIVATE);
    impl_->state.layout_width = static_cast<float>(width);
    impl_->state.buttons.clear();
    detail::layout_tree(impl_->state);
    detail::create_inputs(impl_->state, hwnd_);
    impl_->state.layout_in_progress = false;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

LRESULT CALLBACK XmlUiHost::window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    using namespace detail;
    XmlUiImpl* impl = state_from_hwnd(hwnd);
    if (msg == WM_CREATE) {
        impl = reinterpret_cast<XmlUiImpl*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(impl));
    }
    if (!impl) return DefWindowProcW(hwnd, msg, wparam, lparam);

    XmlUiState& state = impl->state;

    switch (msg) {
    case WM_NCHITTEST: {
        POINT pt_screen{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
        ScreenToClient(hwnd, &pt_screen);
        if (hit_test_button(state, pt_screen) >= 0) return HTCLIENT;
        if (hit_test_input(state, pt_screen)) return HTCLIENT;
        return HTTRANSPARENT;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_SIZE:
        if (!state.paint_ready || state.layout_in_progress) return 0;
        if (state.target) {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            const UINT width = static_cast<UINT32>(rc.right - rc.left);
            const UINT height = static_cast<UINT32>(rc.bottom - rc.top);
            if (width > 0 && height > 0) {
                state.target->Resize(D2D1::SizeU(width, height));
            }
        }
        state.buttons.clear();
        layout_tree(state);
        detail::destroy_inputs(state);
        detail::create_inputs(state, hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_MOUSEMOVE: {
        POINT pt{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
        const int hovered = hit_test_button(state, pt);
        if (hovered != state.hovered_button) {
            state.hovered_button = hovered;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        if (!state.mouse_tracking) {
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            state.mouse_tracking = true;
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        state.mouse_tracking = false;
        if (state.hovered_button != -1) {
            state.hovered_button = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_LBUTTONUP: {
        POINT pt{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
        const int clicked = hit_test_button(state, pt);
        if (clicked >= 0 && impl->owner && impl->owner->button_handler_) {
            for (UiNode* button : state.buttons) {
                if (button && button->button_index == clicked) {
                    impl->owner->button_handler_(to_wide(button->id));
                    break;
                }
            }
        }
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        BeginPaint(hwnd, &ps);
        if (state.paint_ready && ensure_render(state, hwnd) && state.target) {
            state.target->BeginDraw();
            if (state.root) {
                const StyleRule& panel_rule = resolve_rule(state, state.root->class_name);
                if (panel_rule.has_bg) {
                    state.target->Clear(panel_rule.bg_color);
                } else {
                    state.target->Clear(D2D1::ColorF(1.f, 1.f, 1.f, 1.f));
                }
                paint_node(state, *state.root, state.hovered_button);
            }
            state.target->EndDraw();
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        detail::destroy_inputs(state);
        detail::destroy_render(state);
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

} // namespace ogg::ui

#endif
