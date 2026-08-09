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
#include <objidl.h>
#include <gdiplus.h>
#include <wincodec.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "comctl32.lib")

namespace ogg::ui {

namespace detail {

constexpr wchar_t kXmlUiClass[] = L"OGG.XmlUiHost";
constexpr float kDefaultGap = 16.f;
constexpr float kFieldGap = 6.f;
constexpr float kInputBorderWidth = 2.f;
constexpr float kInputDefaultRadius = 6.f;

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
    bool has_font_size = false;
    float font_size = 14.f;
    int font_weight = 400;
    float radius = 0.f;
};

constexpr float kH1FontRatio = 2.0f;
constexpr float kH2FontRatio = 1.5f;
constexpr float kH3FontRatio = 1.25f;

enum class NodeType { Div, Row, Text, Label, Input, TextArea, Button, SmallButton, Gallery, Image, Slider, Select, H1, H2, H3, Range, RadioGroup, Radio };

constexpr float kRangeArrowWidth = 28.f;
constexpr float kRadioCircleSize = 14.f;
constexpr float kRadioLabelGap = 8.f;
constexpr float kRadioItemGap = 24.f;
constexpr float kRowGap = 12.f;
constexpr float kWheelNotchDelta = 120.f;
constexpr UINT_PTR kBusyTimerId = 1;
constexpr UINT_PTR kImageLoadTimerId = 2;
constexpr UINT kMsgImageBitmapReady = WM_APP + 10;

struct UiNode {
    NodeType type = NodeType::Div;
    std::string class_name;
    std::string id;
    std::string text;
    std::string placeholder;
    std::string input_value;
    std::string input_type;
    int textarea_rows = 4;
    std::string image_src;
    bool selected = false;
    bool disabled = false;
    int gallery_columns = 3;
    int slider_min = 0;
    int slider_max = 20;
    int slider_value = 0;
    bool select_fonts = false;
    std::vector<std::string> select_options;
    std::string option_value;
    std::string selected_value;
    UiNode* parent = nullptr;
    std::vector<std::unique_ptr<UiNode>> children;
    D2D1_RECT_F bounds{};
    int button_index = -1;
    int input_index = -1;
};

struct InputControl {
    std::string id;
    HWND hwnd = nullptr;
    D2D1_RECT_F bounds{};
};

struct SliderControl {
    std::string id;
    HWND hwnd = nullptr;
    D2D1_RECT_F bounds{};
};

struct SelectControl {
    std::string id;
    HWND hwnd = nullptr;
    D2D1_RECT_F bounds{};
};

struct RangeControl {
    std::string id;
    HWND edit_hwnd = nullptr;
    D2D1_RECT_F bounds{};
    D2D1_RECT_F up_bounds{};
    D2D1_RECT_F down_bounds{};
    int min_value = 0;
    int max_value = 100;
};

struct XmlUiState {
    std::vector<StyleRule> rules;
    std::unique_ptr<UiNode> root;
    std::vector<InputControl> inputs;
    std::vector<SliderControl> sliders;
    std::vector<SelectControl> selects;
    std::vector<RangeControl> ranges;
    std::vector<UiNode*> buttons;
    std::vector<UiNode*> images;
    int hovered_button = -1;
    int hovered_image = -1;
    int hovered_range = -1;
    int hovered_range_part = -1;
    int focused_input = -1;
    bool mouse_tracking = false;
    bool layout_in_progress = false;
    bool paint_ready = false;
    float layout_width = 320.f;
    float control_width = 160.f;
    float button_width = 200.f;
    float small_button_width = 96.f;
    float label_control_gap = 6.f;
    float scroll_wheel_step = 25.f;
    float scroll_y = 0.f;
    float viewport_height = 0.f;
    float content_height = 0.f;
    bool range_edit_syncing = false;
    InputInsets input_insets{};
    UiTypography typography{};
    ID2D1Factory* factory = nullptr;
    IDWriteFactory* write_factory = nullptr;
    ID2D1HwndRenderTarget* target = nullptr;
    ID2D1SolidColorBrush* text_brush = nullptr;
    ID2D1SolidColorBrush* bg_brush = nullptr;
    ID2D1SolidColorBrush* border_brush = nullptr;
    std::unordered_map<std::string, IDWriteTextFormat*> text_formats;
    std::unordered_map<std::string, ID2D1Bitmap*> image_bitmaps;
    std::unordered_set<std::string> image_loading;
    std::unordered_set<std::string> image_failed;
    float image_load_angle = 0.f;
    HWND host_hwnd = nullptr;
    ULONG_PTR gdiplus_token = 0;
    bool gdiplus_ready = false;
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
        if (value == "transparent") {
            rule.has_bg = false;
        } else if (parse_hex_color(value, rule.bg_color)) {
            rule.has_bg = true;
        }
    } else if (key == "hover_color") {
        if (parse_hex_color(value, rule.hover_color)) rule.has_hover_color = true;
    } else if (key == "hover_bg_color") {
        if (value == "transparent") {
            rule.has_hover_bg = false;
        } else if (parse_hex_color(value, rule.hover_bg_color)) {
            rule.has_hover_bg = true;
        }
    } else if (key == "border_color") {
        if (parse_hex_color(value, rule.border_color)) rule.has_border = true;
    } else if (key == "focus_border_color") {
        if (parse_hex_color(value, rule.focus_border_color)) rule.has_focus_border = true;
    } else if (key == "font_size") {
        rule.font_size = parse_float(value, rule.font_size);
        rule.has_font_size = true;
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
    } else if (key == "rows") {
        node.textarea_rows = static_cast<int>(parse_float(value, static_cast<float>(node.textarea_rows)));
    } else if (key == "src") {
        node.image_src = value;
    } else if (key == "selected") {
        node.selected = (value == "true" || value == "1");
    } else if (key == "columns") {
        node.gallery_columns = static_cast<int>(parse_float(value, static_cast<float>(node.gallery_columns)));
    } else if (key == "min") {
        node.slider_min = static_cast<int>(parse_float(value, static_cast<float>(node.slider_min)));
    } else if (key == "max") {
        node.slider_max = static_cast<int>(parse_float(value, static_cast<float>(node.slider_max)));
    } else if (key == "options" && value == "fonts") {
        node.select_fonts = true;
    } else if (key == "options") {
        node.select_fonts = false;
        node.select_options.clear();
        size_t start = 0;
        while (start <= value.size()) {
            const size_t end = value.find(',', start);
            const size_t slice_end = end == std::string::npos ? value.size() : end;
            std::string item = value.substr(start, slice_end - start);
            while (!item.empty() && std::isspace(static_cast<unsigned char>(item.front()))) item.erase(item.begin());
            while (!item.empty() && std::isspace(static_cast<unsigned char>(item.back()))) item.pop_back();
            if (!item.empty()) node.select_options.push_back(item);
            if (end == std::string::npos) break;
            start = end + 1;
        }
    } else if (key == "value") {
        if (node.type == NodeType::RadioGroup) {
            node.selected_value = value;
        } else if (node.type == NodeType::Radio) {
            node.option_value = value;
        } else if (node.type == NodeType::Select) {
            node.selected_value = value;
        } else {
            node.slider_value = static_cast<int>(parse_float(value, static_cast<float>(node.slider_value)));
        }
    }
}

NodeType tag_to_type(const std::string& tag) {
    if (tag == "Layout") return NodeType::Div;
    if (tag == "Div") return NodeType::Div;
    if (tag == "Row") return NodeType::Row;
    if (tag == "Text") return NodeType::Text;
    if (tag == "Label") return NodeType::Label;
    if (tag == "Input") return NodeType::Input;
    if (tag == "TextArea") return NodeType::TextArea;
    if (tag == "Button") return NodeType::Button;
    if (tag == "SmallButton" || tag == "Btn") return NodeType::SmallButton;
    if (tag == "Gallery") return NodeType::Gallery;
    if (tag == "Image") return NodeType::Image;
    if (tag == "Slider") return NodeType::Slider;
    if (tag == "Select") return NodeType::Select;
    if (tag == "H1") return NodeType::H1;
    if (tag == "H2") return NodeType::H2;
    if (tag == "H3") return NodeType::H3;
    if (tag == "Range") return NodeType::Range;
    if (tag == "RadioGroup") return NodeType::RadioGroup;
    if (tag == "Radio") return NodeType::Radio;
    return NodeType::Div;
}

std::string decode_xml_entities(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '&' || i + 1 >= text.size()) {
            out.push_back(text[i]);
            continue;
        }
        if (text.compare(i, 5, "&amp;") == 0) {
            out.push_back('&');
            i += 4;
        } else if (text.compare(i, 4, "&lt;") == 0) {
            out.push_back('<');
            i += 3;
        } else if (text.compare(i, 4, "&gt;") == 0) {
            out.push_back('>');
            i += 3;
        } else if (text.compare(i, 6, "&quot;") == 0) {
            out.push_back('"');
            i += 5;
        } else if (text.compare(i, 6, "&apos;") == 0) {
            out.push_back('\'');
            i += 5;
        } else {
            out.push_back(text[i]);
        }
    }
    return out;
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
        return { XmlToken::Text, {}, {}, decode_xml_entities(trim(text)) };
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
                attrs[key] = decode_xml_entities(value);
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

std::string wide_to_utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), len, nullptr, nullptr);
    return out;
}

float effective_font_size(const XmlUiState& state, NodeType type, const StyleRule& rule) {
    if (rule.has_font_size) return rule.font_size;
    switch (type) {
    case NodeType::Input:
    case NodeType::TextArea:
        return state.typography.input_font_size;
    case NodeType::Button:
    case NodeType::SmallButton:
        return state.typography.button_font_size;
    case NodeType::H1:
        return state.typography.label_font_size * kH1FontRatio;
    case NodeType::H2:
        return state.typography.label_font_size * kH2FontRatio;
    case NodeType::H3:
        return state.typography.label_font_size * kH3FontRatio;
    default:
        return state.typography.label_font_size;
    }
}

void clear_text_formats(XmlUiState& state) {
    for (auto& pair : state.text_formats) {
        if (pair.second) pair.second->Release();
    }
    state.text_formats.clear();
}

struct FontEnumCtx {
    std::vector<std::wstring>* fonts = nullptr;
};

int CALLBACK enum_font_proc(const LOGFONTW* lf, const TEXTMETRICW*, DWORD, LPARAM lparam) {
    auto* ctx = reinterpret_cast<FontEnumCtx*>(lparam);
    if (!ctx || !ctx->fonts || !lf || lf->lfFaceName[0] == L'\0') return 1;
    if (lf->lfFaceName[0] == L'@') return 1;
    const std::wstring name(lf->lfFaceName);
    if (std::find(ctx->fonts->begin(), ctx->fonts->end(), name) == ctx->fonts->end()) {
        ctx->fonts->push_back(name);
    }
    return 1;
}

const std::vector<std::wstring>& system_fonts() {
    static std::vector<std::wstring> fonts = []() {
        std::vector<std::wstring> list;
        FontEnumCtx ctx{ &list };
        HDC hdc = GetDC(nullptr);
        if (hdc) {
            LOGFONTW lf{};
            lf.lfCharSet = DEFAULT_CHARSET;
            EnumFontFamiliesExW(hdc, &lf, enum_font_proc, reinterpret_cast<LPARAM>(&ctx), 0);
            ReleaseDC(nullptr, hdc);
        }
        std::sort(list.begin(), list.end());
        return list;
    }();
    return fonts;
}

IDWriteTextFormat* format_for_text(XmlUiState& state, NodeType type, const StyleRule& rule) {
    if (!ensure_write_factory(state)) return nullptr;
    const float size = effective_font_size(state, type, rule);
    const std::wstring& family = state.typography.font_family.empty() ? L"Segoe UI" : state.typography.font_family;
    const std::string key = wide_to_utf8(family) + "|" + rule.name + "|" + std::to_string(size) + "|" + std::to_string(rule.font_weight);
    auto it = state.text_formats.find(key);
    if (it != state.text_formats.end()) return it->second;

    IDWriteTextFormat* format = nullptr;
    const DWRITE_FONT_WEIGHT weight = rule.font_weight >= 600 ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
    if (FAILED(state.write_factory->CreateTextFormat(
            family.c_str(),
            nullptr,
            weight,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            size,
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

float measure_text_height(XmlUiState& state, NodeType type, const std::wstring& text, const StyleRule& rule) {
    const float size = effective_font_size(state, type, rule);
    IDWriteTextFormat* format = format_for_text(state, type, rule);
    if (!format || text.empty()) return size * 1.2f;
    IDWriteTextLayout* layout = nullptr;
    if (FAILED(state.write_factory->CreateTextLayout(
            text.c_str(),
            static_cast<UINT32>(text.size()),
            format,
            280.f,
            1000.f,
            &layout))) {
        return size * 1.2f;
    }
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    layout->Release();
    return metrics.height;
}

float input_line_height(XmlUiState& state, const StyleRule& rule) {
    return measure_text_height(state, NodeType::Input, L"Ag", rule);
}

float textarea_outer_height(XmlUiState& state, const UiNode& node, const StyleRule& rule) {
    if (rule.height > 0.f) return rule.height;
    const int rows = node.textarea_rows > 0 ? node.textarea_rows : 4;
    const float line_h = input_line_height(state, rule);
    return static_cast<float>(rows) * line_h
        + 2.f * kInputBorderWidth
        + static_cast<float>(state.input_insets.top + state.input_insets.bottom);
}

std::wstring to_wide(const std::string& text) {
    if (text.empty()) return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    if (len <= 0) return {};
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), len);
    return out;
}

float effective_field_width(const XmlUiState& state, const StyleRule& rule) {
    if (rule.width > 0.f) return rule.width;
    return state.control_width > 0.f ? state.control_width : 320.f;
}

float effective_button_width(const XmlUiState& state, const StyleRule& rule) {
    if (rule.width > 0.f) return rule.width;
    return state.button_width > 0.f ? state.button_width : 140.f;
}

float effective_small_button_width(const XmlUiState& state, const StyleRule& rule) {
    if (rule.width > 0.f) return rule.width;
    return state.small_button_width > 0.f ? state.small_button_width : 80.f;
}

float max_scroll_y(const XmlUiState& state) {
    return std::max(0.f, state.content_height - state.viewport_height);
}

void update_scroll_metrics(XmlUiState& state) {
    state.content_height = state.root ? state.root->bounds.bottom : 0.f;
    const float max_y = max_scroll_y(state);
    if (state.scroll_y > max_y) state.scroll_y = max_y;
    if (state.scroll_y < 0.f) state.scroll_y = 0.f;
}

D2D1_RECT_F scroll_rect(const D2D1_RECT_F& rect, float scroll_y) {
    return D2D1::RectF(rect.left, rect.top - scroll_y, rect.right, rect.bottom - scroll_y);
}

POINT content_point(POINT pt, float scroll_y) {
    return POINT{ pt.x, pt.y + static_cast<LONG>(scroll_y) };
}

bool rect_intersects_viewport(const D2D1_RECT_F& rect, float scroll_y, float viewport_height) {
    const float top = rect.top - scroll_y;
    const float bottom = rect.bottom - scroll_y;
    return bottom > 0.f && top < viewport_height;
}

int clamp_range_value(int value, int min_value, int max_value) {
    if (min_value > max_value) return value;
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

UiNode* find_node_by_id(UiNode* node, const std::string& id);

void sync_range_nodes_from_edits(XmlUiState& state) {
    if (!state.root) return;
    for (const auto& range : state.ranges) {
        if (!range.edit_hwnd || range.id.empty()) continue;
        wchar_t buffer[32]{};
        GetWindowTextW(range.edit_hwnd, buffer, 32);
        int value = _wtoi(buffer);
        value = clamp_range_value(value, range.min_value, range.max_value);
        if (UiNode* node = find_node_by_id(state.root.get(), range.id)) {
            node->slider_value = value;
        }
    }
}

void sync_select_nodes_from_combos(XmlUiState& state) {
    if (!state.root) return;
    for (const auto& select : state.selects) {
        if (!select.hwnd || select.id.empty()) continue;
        const int idx = static_cast<int>(SendMessageW(select.hwnd, CB_GETCURSEL, 0, 0));
        if (idx < 0) continue;
        wchar_t buffer[256]{};
        SendMessageW(select.hwnd, CB_GETLBTEXT, static_cast<WPARAM>(idx), reinterpret_cast<LPARAM>(buffer));
        if (UiNode* node = find_node_by_id(state.root.get(), select.id)) {
            node->selected_value = wide_to_utf8(buffer);
        }
    }
}

void sync_input_nodes_from_edits(XmlUiState& state) {
    if (!state.root) return;
    for (const auto& input : state.inputs) {
        if (!input.hwnd || input.id.empty()) continue;
        const int len = GetWindowTextLengthW(input.hwnd);
        if (len < 0) continue;
        if (len == 0) {
            if (UiNode* node = find_node_by_id(state.root.get(), input.id)) {
                node->input_value.clear();
            }
            continue;
        }
        if (len > 65536) continue;
        std::wstring text(static_cast<size_t>(len) + 1u, L'\0');
        const int copied = GetWindowTextW(input.hwnd, text.data(), len + 1);
        if (copied < 0) continue;
        text.resize(static_cast<size_t>(copied));
        if (UiNode* node = find_node_by_id(state.root.get(), input.id)) {
            node->input_value = wide_to_utf8(text);
        }
    }
}

void set_range_edit_value(XmlUiState& state, RangeControl& range, int value) {
    const int clamped = clamp_range_value(value, range.min_value, range.max_value);
    if (!range.id.empty() && state.root) {
        if (UiNode* node = find_node_by_id(state.root.get(), range.id)) {
            node->slider_value = clamped;
        }
    }
    if (!range.edit_hwnd) return;
    const std::wstring text = std::to_wstring(clamped);
    wchar_t current[32]{};
    GetWindowTextW(range.edit_hwnd, current, 32);
    if (text == current) return;
    state.range_edit_syncing = true;
    SetWindowTextW(range.edit_hwnd, text.c_str());
    state.range_edit_syncing = false;
}

float measure_node_height(XmlUiState& state, const UiNode& node, float content_width);

float node_margin_bottom(const XmlUiState& state, const UiNode& node, const StyleRule& rule) {
    if (node.type == NodeType::Label) return state.label_control_gap;
    return rule.margin.bottom;
}

float stack_gap_before_child(const XmlUiState& state, const UiNode* previous, const UiNode& child, const StyleRule& child_rule) {
    if (previous && previous->type == NodeType::Label) {
        return child_rule.margin.top;
    }
    if (previous && previous->type == NodeType::Row && child.class_name == "key-status") {
        return 4.f;
    }
    return std::max(child_rule.margin.top, kDefaultGap) + child_rule.margin.top;
}

float measure_children_height(XmlUiState& state, const UiNode& node, float content_width) {
    float total = 0.f;
    bool first = true;
    const UiNode* previous = nullptr;
    for (const auto& child : node.children) {
        const StyleRule& child_rule = resolve_rule(state, child->class_name);
        if (!first) {
            total += stack_gap_before_child(state, previous, *child, child_rule);
        } else {
            total += child_rule.margin.top;
        }
        total += measure_node_height(state, *child, content_width - child_rule.margin.left - child_rule.margin.right);
        total += node_margin_bottom(state, *child, child_rule);
        previous = child.get();
        first = false;
    }
    return total;
}

float measure_node_height(XmlUiState& state, const UiNode& node, float content_width) {
    const StyleRule& rule = resolve_rule(state, node.class_name);
    switch (node.type) {
    case NodeType::Div:
        return rule.padding.top + measure_children_height(state, node, content_width - rule.padding.left - rule.padding.right) + rule.padding.bottom;
    case NodeType::Row: {
        float max_h = 0.f;
        for (const auto& child : node.children) {
            max_h = std::max(max_h, measure_node_height(state, *child, content_width));
        }
        return rule.margin.top + rule.padding.top + max_h + rule.padding.bottom + rule.margin.bottom;
    }
    case NodeType::Gallery: {
        if (node.children.empty()) return rule.padding.top + rule.padding.bottom;
        const int columns = node.gallery_columns > 0 ? node.gallery_columns : 3;
        float row_height = 0.f;
        float row_width_used = 0.f;
        float total = 0.f;
        bool first_row = true;
        for (const auto& child : node.children) {
            const StyleRule& child_rule = resolve_rule(state, child->class_name);
            const float item_w = child_rule.width > 0.f ? child_rule.width : 220.f;
            const float item_h = child_rule.height > 0.f ? child_rule.height : 140.f;
            if (row_width_used > 0.f) row_width_used += 16.f;
            if (row_width_used + item_w > content_width && row_width_used > 0.f) {
                total += row_height + 16.f;
                row_width_used = 0.f;
                row_height = 0.f;
                first_row = false;
            }
            row_width_used += item_w;
            row_height = std::max(row_height, item_h + child_rule.margin.top + child_rule.margin.bottom);
            if (first_row && row_width_used > 0.f) first_row = false;
        }
        total += row_height;
        return rule.padding.top + total + rule.padding.bottom;
    }
    case NodeType::Image: {
        const float h = rule.height > 0.f ? rule.height : 140.f;
        return rule.margin.top + h + rule.margin.bottom;
    }
    case NodeType::Text:
    case NodeType::Label:
    case NodeType::H1:
    case NodeType::H2:
    case NodeType::H3:
        if (node.text.empty()) {
            return rule.margin.top + rule.margin.bottom;
        }
        return rule.padding.top + measure_text_height(state, node.type, to_wide(node.text), rule) + rule.padding.bottom;
    case NodeType::Input:
    case NodeType::Select:
    case NodeType::Range:
        return rule.padding.top + (rule.height > 0.f ? rule.height : 36.f) + rule.padding.bottom;
    case NodeType::TextArea:
        return rule.padding.top + textarea_outer_height(state, node, rule) + rule.padding.bottom;
    case NodeType::Button:
        return rule.padding.top + (rule.height > 0.f ? rule.height : 38.f) + rule.padding.bottom;
    case NodeType::SmallButton:
        return rule.padding.top + (rule.height > 0.f ? rule.height : 30.f) + rule.padding.bottom;
    case NodeType::RadioGroup: {
        if (node.children.empty()) return rule.padding.top + rule.padding.bottom;
        float row_h = 0.f;
        for (const auto& child : node.children) {
            if (child->type != NodeType::Radio) continue;
            const StyleRule& child_rule = resolve_rule(state, child->class_name);
            row_h = std::max(row_h, measure_text_height(state, NodeType::Label, to_wide(child->text), child_rule) + child_rule.margin.top + child_rule.margin.bottom);
        }
        return rule.margin.top + rule.padding.top + std::max(row_h, 24.f) + rule.padding.bottom + rule.margin.bottom;
    }
    case NodeType::Radio:
        return rule.margin.top + std::max(24.f, measure_text_height(state, NodeType::Label, to_wide(node.text), rule)) + rule.margin.bottom;
    case NodeType::Slider:
        return rule.margin.top + (rule.height > 0.f ? rule.height : 28.f) + rule.margin.bottom;
    }
    return 0.f;
}

void layout_gallery_children(XmlUiState& state, UiNode& gallery, float x, float y, float width) {
    const StyleRule& rule = resolve_rule(state, gallery.class_name);
    const int columns = gallery.gallery_columns > 0 ? gallery.gallery_columns : 3;
    float cursor_x = x + rule.padding.left;
    float cursor_y = y + rule.padding.top;
    float row_height = 0.f;
    int col = 0;

    for (auto& child : gallery.children) {
        const StyleRule& child_rule = resolve_rule(state, child->class_name);
        const float item_w = child_rule.width > 0.f ? child_rule.width : 220.f;
        const float item_h = child_rule.height > 0.f ? child_rule.height : 140.f;
        if (col >= columns) {
            col = 0;
            cursor_x = x + rule.padding.left;
            cursor_y += row_height + 16.f;
            row_height = 0.f;
        }
        const float local_x = cursor_x + child_rule.margin.left;
        const float local_y = cursor_y + child_rule.margin.top;
        child->bounds = D2D1::RectF(local_x, local_y, local_x + item_w, local_y + item_h);
        row_height = std::max(row_height, item_h + child_rule.margin.top + child_rule.margin.bottom);
        cursor_x += item_w + 16.f + child_rule.margin.left + child_rule.margin.right;
        ++col;
    }

    const float inner_h = (cursor_y + row_height) - (y + rule.padding.top);
    gallery.bounds = D2D1::RectF(x, y, x + width, y + rule.padding.top + inner_h + rule.padding.bottom);
}

float measure_radio_label_width(XmlUiState& state, const UiNode& radio) {
    const StyleRule& rule = resolve_rule(state, radio.class_name);
    if (radio.text.empty()) return 40.f;
    IDWriteTextFormat* format = format_for_text(state, NodeType::Label, rule);
    if (!format || !ensure_write_factory(state)) return 60.f;
    const std::wstring text = to_wide(radio.text);
    IDWriteTextLayout* layout = nullptr;
    if (FAILED(state.write_factory->CreateTextLayout(
            text.c_str(),
            static_cast<UINT32>(text.size()),
            format,
            1000.f,
            100.f,
            &layout))) {
        return 60.f;
    }
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    layout->Release();
    return metrics.width;
}

void layout_radio_group(XmlUiState& state, UiNode& group, float x, float y, float width) {
    const StyleRule& rule = resolve_rule(state, group.class_name);
    const float local_x = x + rule.margin.left;
    const float local_y = y + rule.margin.top;
    float cursor_x = local_x + rule.padding.left;
    const float cursor_y = local_y + rule.padding.top;
    float row_h = 0.f;

    for (auto& child : group.children) {
        if (!child || child->type != NodeType::Radio) continue;
        const StyleRule& child_rule = resolve_rule(state, child->class_name);
        const float label_w = measure_radio_label_width(state, *child);
        const float item_w = kRadioCircleSize + kRadioLabelGap + label_w;
        const float item_h = std::max(24.f, measure_text_height(state, NodeType::Label, to_wide(child->text), child_rule));
        child->bounds = D2D1::RectF(
            cursor_x + child_rule.margin.left,
            cursor_y + child_rule.margin.top,
            cursor_x + child_rule.margin.left + item_w,
            cursor_y + child_rule.margin.top + item_h
        );
        row_h = std::max(row_h, item_h + child_rule.margin.top + child_rule.margin.bottom);
        cursor_x += item_w + kRadioItemGap + child_rule.margin.left + child_rule.margin.right;
    }

    const float inner_w = std::max(0.f, cursor_x - (local_x + rule.padding.left));
    group.bounds = D2D1::RectF(
        local_x,
        local_y,
        local_x + std::max(inner_w, width - rule.margin.left - rule.margin.right),
        local_y + rule.padding.top + row_h + rule.padding.bottom
    );
}

float measure_node_height(XmlUiState& state, const UiNode& node, float content_width);

void layout_node(XmlUiState& state, UiNode& node, float x, float y, float width);

float row_child_height(XmlUiState& state, const UiNode& child) {
    const StyleRule& rule = resolve_rule(state, child.class_name);
    switch (child.type) {
    case NodeType::Input:
    case NodeType::TextArea:
    case NodeType::Select:
    case NodeType::Range:
        if (child.type == NodeType::TextArea) {
            return textarea_outer_height(state, child, rule);
        }
        return rule.height > 0.f ? rule.height : 36.f;
    case NodeType::Button:
        return rule.height > 0.f ? rule.height : 38.f;
    case NodeType::SmallButton:
        return rule.height > 0.f ? rule.height : 30.f;
    default:
        return 0.f;
    }
}

void layout_row(XmlUiState& state, UiNode& row, float x, float y, float width) {
    const StyleRule& rule = resolve_rule(state, row.class_name);
    const float local_x = x + rule.margin.left;
    const float local_y = y + rule.margin.top;
    const float inner_w = width - rule.margin.left - rule.margin.right;
    const float content_w = inner_w - rule.padding.left - rule.padding.right;

    float buttons_w = 0.f;
    int field_count = 0;
    for (const auto& child : row.children) {
        if (!child) continue;
        const StyleRule& child_rule = resolve_rule(state, child->class_name);
        if (child->type == NodeType::Button) {
            buttons_w += effective_button_width(state, child_rule) + kRowGap;
        } else if (child->type == NodeType::SmallButton) {
            buttons_w += effective_small_button_width(state, child_rule) + kRowGap;
        } else if (child->type == NodeType::Input || child->type == NodeType::TextArea || child->type == NodeType::Select || child->type == NodeType::Range) {
            ++field_count;
        }
    }
    if (buttons_w > 0.f) buttons_w -= kRowGap;

    float field_w = field_count > 0 ? (content_w - buttons_w) / static_cast<float>(field_count) : 0.f;
    if (field_w < 80.f) field_w = 80.f;

    float row_content_h = 0.f;
    for (const auto& child : row.children) {
        if (child) row_content_h = std::max(row_content_h, row_child_height(state, *child));
    }

    float cursor_x = local_x + rule.padding.left;
    const float cursor_y = local_y + rule.padding.top;

    for (auto& child : row.children) {
        if (!child) continue;
        const StyleRule& child_rule = resolve_rule(state, child->class_name);
        float child_w = content_w;
        if (child->type == NodeType::Button) {
            child_w = effective_button_width(state, child_rule);
        } else if (child->type == NodeType::SmallButton) {
            child_w = effective_small_button_width(state, child_rule);
        } else if (child->type == NodeType::Input || child->type == NodeType::TextArea || child->type == NodeType::Select || child->type == NodeType::Range) {
            child_w = field_w;
        }
        const float child_h = row_child_height(state, *child);
        const float y_offset = row_content_h > child_h ? (row_content_h - child_h) * 0.5f : 0.f;
        layout_node(state, *child, cursor_x, cursor_y + y_offset, child_w);
        cursor_x = child->bounds.right + kRowGap;
    }

    row.bounds = D2D1::RectF(
        local_x,
        local_y,
        local_x + inner_w,
        local_y + rule.padding.top + row_content_h + rule.padding.bottom
    );
}

void layout_node(XmlUiState& state, UiNode& node, float x, float y, float width) {
    const StyleRule& rule = resolve_rule(state, node.class_name);
    const float local_x = x + rule.margin.left;
    const float local_y = y + rule.margin.top;
    const float inner_w = width - rule.margin.left - rule.margin.right;
    const float inner_h = measure_node_height(state, node, inner_w) - rule.margin.top - rule.margin.bottom;

    node.bounds = D2D1::RectF(local_x, local_y, local_x + inner_w, local_y + inner_h);

    const bool in_row = node.parent && node.parent->type == NodeType::Row;

    if (node.type == NodeType::Input || node.type == NodeType::TextArea || node.type == NodeType::Select || node.type == NodeType::Range) {
        const float content_w = std::max(0.f, inner_w - rule.padding.left - rule.padding.right);
        const float preferred_w = effective_field_width(state, rule);
        const float w = in_row ? inner_w : std::min(preferred_w, content_w > 0.f ? content_w : preferred_w);
        float h = rule.height > 0.f ? rule.height : 36.f;
        if (node.type == NodeType::TextArea) {
            h = textarea_outer_height(state, node, rule);
        }
        node.bounds = D2D1::RectF(
            local_x,
            local_y + rule.padding.top,
            local_x + w,
            local_y + rule.padding.top + h
        );
    } else if (node.type == NodeType::Button) {
        const float w = in_row ? inner_w : effective_button_width(state, rule);
        const float h = rule.height > 0.f ? rule.height : 38.f;
        node.bounds = D2D1::RectF(
            local_x + rule.padding.left,
            local_y + rule.padding.top,
            local_x + rule.padding.left + w,
            local_y + rule.padding.top + h
        );
    } else if (node.type == NodeType::SmallButton) {
        const float w = in_row ? inner_w : effective_small_button_width(state, rule);
        const float h = rule.height > 0.f ? rule.height : 30.f;
        node.bounds = D2D1::RectF(
            local_x + rule.padding.left,
            local_y + rule.padding.top,
            local_x + rule.padding.left + w,
            local_y + rule.padding.top + h
        );
    } else if (node.type == NodeType::Slider) {
        const float h = rule.height > 0.f ? rule.height : 28.f;
        const float w = rule.width > 0.f ? rule.width : inner_w;
        node.bounds = D2D1::RectF(
            local_x,
            local_y + rule.margin.top,
            local_x + w,
            local_y + rule.margin.top + h
        );
    }

    if (node.type == NodeType::Gallery) {
        layout_gallery_children(state, node, local_x, local_y, inner_w);
        return;
    }

    if (node.type == NodeType::RadioGroup) {
        layout_radio_group(state, node, local_x, local_y, inner_w);
        return;
    }

    if (node.type == NodeType::Row) {
        layout_row(state, node, local_x, local_y, inner_w);
        return;
    }

    if (node.type == NodeType::Div) {
        float child_y = local_y + rule.padding.top;
        bool first = true;
        const UiNode* previous = nullptr;
        for (auto& child : node.children) {
            const StyleRule& child_rule = resolve_rule(state, child->class_name);
            if (!first) {
                child_y += stack_gap_before_child(state, previous, *child, child_rule);
            } else {
                child_y += child_rule.margin.top;
            }
            layout_node(state, *child, local_x + rule.padding.left, child_y, inner_w - rule.padding.left - rule.padding.right);
            child_y += child->bounds.bottom - child->bounds.top + node_margin_bottom(state, *child, child_rule);
            previous = child.get();
            first = false;
        }
    }
}

void layout_tree(XmlUiState& state) {
    if (!state.root) return;
    layout_node(state, *state.root, 0.f, 0.f, state.layout_width);
}

void destroy_render(XmlUiState& state) {
    clear_text_formats(state);
    for (auto& pair : state.image_bitmaps) {
        if (pair.second) pair.second->Release();
    }
    state.image_bitmaps.clear();
    if (state.gdiplus_ready) {
        Gdiplus::GdiplusShutdown(state.gdiplus_token);
        state.gdiplus_ready = false;
        state.gdiplus_token = 0;
    }
    if (state.text_brush) { state.text_brush->Release(); state.text_brush = nullptr; }
    if (state.bg_brush) { state.bg_brush->Release(); state.bg_brush = nullptr; }
    if (state.border_brush) { state.border_brush->Release(); state.border_brush = nullptr; }
    if (state.target) { state.target->Release(); state.target = nullptr; }
}

bool ensure_render(XmlUiState& state, HWND hwnd) {
    if (!state.gdiplus_ready) {
        Gdiplus::GdiplusStartupInput input;
        if (Gdiplus::GdiplusStartup(&state.gdiplus_token, &input, nullptr) == Gdiplus::Ok) {
            state.gdiplus_ready = true;
        }
    }
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

void stroke_rounded_rect(ID2D1HwndRenderTarget* target, const D2D1_RECT_F& rect, float radius, float stroke_width, ID2D1Brush* brush) {
    if (radius > 0.f) {
        D2D1_ROUNDED_RECT round{ rect, radius, radius };
        target->DrawRoundedRectangle(round, brush, stroke_width);
    } else {
        target->DrawRectangle(rect, brush, stroke_width);
    }
}

D2D1_RECT_F input_inner_bounds(const D2D1_RECT_F& outer, const InputInsets& insets) {
    const float border = kInputBorderWidth;
    return D2D1::RectF(
        outer.left + border + static_cast<float>(insets.left),
        outer.top + border + static_cast<float>(insets.top),
        outer.right - border - static_cast<float>(insets.right),
        outer.bottom - border - static_cast<float>(insets.bottom)
    );
}

D2D1_RECT_F range_edit_inner_bounds(const D2D1_RECT_F& outer, const InputInsets& insets) {
    const D2D1_RECT_F value_outer{
        outer.left,
        outer.top,
        outer.right - kRangeArrowWidth,
        outer.bottom,
    };
    return input_inner_bounds(value_outer, insets);
}

LRESULT CALLBACK input_edit_subclass(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR ref_data) {
    (void)id;
    (void)ref_data;
    const LRESULT result = DefSubclassProc(hwnd, msg, wparam, lparam);
    if (msg == WM_CHAR && wparam == '\b') {
        HWND parent = GetParent(hwnd);
        if (parent) InvalidateRect(parent, nullptr, FALSE);
    } else if (msg == WM_KEYDOWN && wparam == VK_DELETE) {
        HWND parent = GetParent(hwnd);
        if (parent) InvalidateRect(parent, nullptr, FALSE);
    }
    return result;
}

std::wstring image_path_wide(const std::string& path) {
    if (path.empty()) return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), static_cast<int>(path.size()), nullptr, 0);
    if (len <= 0) return {};
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), static_cast<int>(path.size()), out.data(), len);
    return out;
}

ID2D1Bitmap* create_bitmap_from_hbitmap(XmlUiState& state, ID2D1HwndRenderTarget* target, HBITMAP hbitmap) {
    if (!target || !hbitmap) return nullptr;

    IWICImagingFactory* wic_factory = nullptr;
    if (FAILED(CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_IWICImagingFactory,
            reinterpret_cast<void**>(&wic_factory)))) {
        return nullptr;
    }

    IWICBitmap* wic_bitmap = nullptr;
    if (FAILED(wic_factory->CreateBitmapFromHBITMAP(hbitmap, nullptr, WICBitmapIgnoreAlpha, &wic_bitmap))) {
        wic_factory->Release();
        return nullptr;
    }

    ID2D1Bitmap* bitmap = nullptr;
    if (FAILED(target->CreateBitmapFromWicBitmap(wic_bitmap, nullptr, &bitmap))) {
        wic_bitmap->Release();
        wic_factory->Release();
        return nullptr;
    }
    wic_bitmap->Release();
    wic_factory->Release();
    return bitmap;
}

struct ImageLoadResult {
    std::string src;
    HBITMAP hbitmap = nullptr;
};

struct ImageLoadWork {
    std::string src;
    HWND notify_hwnd = nullptr;
};

DWORD WINAPI image_load_worker(LPVOID param) {
    ImageLoadWork* work = static_cast<ImageLoadWork*>(param);
    if (!work) return 0;

    ImageLoadResult* result = new ImageLoadResult{};
    result->src = work->src;
    const HWND notify_hwnd = work->notify_hwnd;
    delete work;

    const HRESULT com_hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool com_should_uninit = (com_hr == S_OK);

    ULONG_PTR gdiplus_token = 0;
    Gdiplus::GdiplusStartupInput gdiplus_input;
    if (Gdiplus::GdiplusStartup(&gdiplus_token, &gdiplus_input, nullptr) == Gdiplus::Ok) {
        const std::wstring wide = image_path_wide(result->src);
        if (!wide.empty()) {
            Gdiplus::Bitmap source(wide.c_str());
            if (source.GetLastStatus() == Gdiplus::Ok) {
                source.GetHBITMAP(Gdiplus::Color(255, 0, 0, 0), &result->hbitmap);
            }
        }
        Gdiplus::GdiplusShutdown(gdiplus_token);
    }

    if (com_should_uninit) CoUninitialize();

    if (notify_hwnd) {
        PostMessageW(notify_hwnd, kMsgImageBitmapReady, 0, reinterpret_cast<LPARAM>(result));
    } else {
        if (result->hbitmap) DeleteObject(result->hbitmap);
        delete result;
    }
    return 0;
}

void request_image_load(XmlUiState& state, HWND hwnd, const std::string& src) {
    if (src.empty() || !hwnd) return;
    if (state.image_bitmaps.find(src) != state.image_bitmaps.end()) return;
    if (state.image_loading.find(src) != state.image_loading.end()) return;
    if (state.image_failed.find(src) != state.image_failed.end()) return;

    state.image_loading.insert(src);
    ImageLoadWork* work = new ImageLoadWork{};
    work->src = src;
    work->notify_hwnd = hwnd;
    HANDLE thread = CreateThread(nullptr, 0, image_load_worker, work, 0, nullptr);
    if (!thread) {
        state.image_loading.erase(src);
        state.image_failed.insert(src);
        delete work;
        return;
    }
    CloseHandle(thread);
    SetTimer(hwnd, kImageLoadTimerId, 50, nullptr);
}

void clear_image_load_state(XmlUiState& state, HWND hwnd) {
    state.image_loading.clear();
    state.image_failed.clear();
    state.image_load_angle = 0.f;
    if (hwnd) KillTimer(hwnd, kImageLoadTimerId);
}

ID2D1Bitmap* bitmap_for_image(XmlUiState& state, ID2D1HwndRenderTarget* target, const std::string& src) {
    if (!target || src.empty()) return nullptr;
    auto it = state.image_bitmaps.find(src);
    if (it != state.image_bitmaps.end()) return it->second;
    if (state.host_hwnd) {
        request_image_load(state, state.host_hwnd, src);
    }
    return nullptr;
}

void draw_image_cover(ID2D1HwndRenderTarget* target, ID2D1Bitmap* bitmap, const D2D1_RECT_F& rect) {
    if (!target || !bitmap) return;
    D2D1_SIZE_F size = bitmap->GetSize();
    if (size.width <= 0.f || size.height <= 0.f) return;

    const float rect_w = rect.right - rect.left;
    const float rect_h = rect.bottom - rect.top;
    if (rect_w <= 0.f || rect_h <= 0.f) return;

    const float scale = std::max(rect_w / size.width, rect_h / size.height);
    const float draw_w = size.width * scale;
    const float draw_h = size.height * scale;
    const float draw_x = rect.left + (rect_w - draw_w) * 0.5f;
    const float draw_y = rect.top + (rect_h - draw_h) * 0.5f;
    D2D1_RECT_F dest{ draw_x, draw_y, draw_x + draw_w, draw_y + draw_h };
    target->DrawBitmap(bitmap, dest, 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, nullptr);
}

void paint_arc_spinner(
    ID2D1HwndRenderTarget* target,
    ID2D1Factory* factory,
    float cx,
    float cy,
    float radius,
    float angle_deg,
    D2D1_COLOR_F color,
    float stroke = 2.5f
);

void paint_node(XmlUiState& state, const UiNode& node, int hovered_button, int hovered_image) {
    const StyleRule& rule = resolve_rule(state, node.class_name);
    auto* target = state.target;
    const bool visible = rect_intersects_viewport(node.bounds, state.scroll_y, state.viewport_height);
    const D2D1_RECT_F bounds = scroll_rect(node.bounds, state.scroll_y);

    if (node.type == NodeType::Div && rule.has_bg && visible) {
        ID2D1SolidColorBrush* brush = make_brush(target, rule.bg_color);
        if (brush) {
            draw_rounded_rect(target, bounds, rule.radius, brush);
            release_brush(brush);
        }
    }

    if ((node.type == NodeType::Text || node.type == NodeType::Label ||
         node.type == NodeType::H1 || node.type == NodeType::H2 || node.type == NodeType::H3) && visible) {
        IDWriteTextFormat* format = format_for_text(state, node.type, rule);
        if (format) {
            const D2D1_COLOR_F color = rule.has_color ? rule.color : D2D1::ColorF(0.12f, 0.12f, 0.12f, 1.f);
            ID2D1SolidColorBrush* brush = make_brush(target, color);
            if (brush) {
                const std::wstring text = to_wide(node.text);
                D2D1_RECT_F text_rect{
                    bounds.left + rule.padding.left,
                    bounds.top + rule.padding.top,
                    bounds.right - rule.padding.right,
                    bounds.bottom - rule.padding.bottom,
                };
                target->DrawText(text.c_str(), static_cast<UINT32>(text.size()), format, text_rect, brush);
                release_brush(brush);
            }
        }
    }

    if ((node.type == NodeType::Button || node.type == NodeType::SmallButton) && visible) {
        const NodeType text_type = node.type == NodeType::SmallButton ? NodeType::SmallButton : NodeType::Button;
        const bool hovered = !node.disabled && node.button_index >= 0 && node.button_index == hovered_button;
        const bool active = !node.disabled && (node.selected || hovered);
        D2D1_COLOR_F fg = active && rule.has_hover_color ? rule.hover_color
            : (rule.has_color ? rule.color : D2D1::ColorF(0.05f, 0.07f, 0.09f, 1.f));
        if (node.disabled) {
            fg = D2D1::ColorF(0.61f, 0.64f, 0.69f, 1.f);
        }

        bool draw_bg = false;
        D2D1_COLOR_F bg{};
        if (node.disabled) {
            draw_bg = true;
            bg = D2D1::ColorF(0.90f, 0.91f, 0.92f, 1.f);
        } else if (node.selected) {
            draw_bg = true;
            bg = rule.has_bg ? rule.bg_color : D2D1::ColorF(0.35f, 0.75f, 1.f, 1.f);
        } else if (hovered && rule.has_hover_bg) {
            draw_bg = true;
            bg = rule.hover_bg_color;
        } else if (rule.has_bg) {
            draw_bg = true;
            bg = rule.bg_color;
        }
        if (draw_bg) {
            ID2D1SolidColorBrush* bg_brush = make_brush(target, bg);
            if (bg_brush) {
                draw_rounded_rect(target, bounds, rule.radius > 0.f ? rule.radius : 6.f, bg_brush);
                release_brush(bg_brush);
            }
        }
        if (rule.has_border) {
            ID2D1SolidColorBrush* border_brush = make_brush(target, rule.border_color);
            if (border_brush) {
                const float radius = rule.radius > 0.f ? rule.radius : 6.f;
                D2D1_ROUNDED_RECT round{ bounds, radius, radius };
                target->DrawRoundedRectangle(round, border_brush, kInputBorderWidth);
                release_brush(border_brush);
            }
        }

        IDWriteTextFormat* format = format_for_text(state, text_type, rule);
        if (format) {
            format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            ID2D1SolidColorBrush* text_brush = make_brush(target, fg);
            if (text_brush) {
                const std::wstring text = to_wide(node.text);
                target->DrawText(text.c_str(), static_cast<UINT32>(text.size()), format, bounds, text_brush);
                release_brush(text_brush);
            }
            format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        }
    }

    if (node.type == NodeType::Radio && visible) {
        const bool selected = node.parent && node.parent->type == NodeType::RadioGroup &&
            node.parent->selected_value == node.option_value;
        const bool hovered = node.button_index >= 0 && node.button_index == state.hovered_button;
        const D2D1_COLOR_F border = selected || hovered
            ? D2D1::ColorF(0.35f, 0.75f, 1.f, 1.f)
            : D2D1::ColorF(0.65f, 0.68f, 0.72f, 1.f);
        const float cy = (bounds.top + bounds.bottom) * 0.5f;
        const float cx = bounds.left + kRadioCircleSize * 0.5f;
        const D2D1_ELLIPSE circle{ D2D1::Point2F(cx, cy), kRadioCircleSize * 0.5f, kRadioCircleSize * 0.5f };
        ID2D1SolidColorBrush* border_brush = make_brush(target, border);
        if (border_brush) {
            target->DrawEllipse(circle, border_brush, 1.5f);
            release_brush(border_brush);
        }
        if (selected) {
            ID2D1SolidColorBrush* fill_brush = make_brush(target, D2D1::ColorF(0.35f, 0.75f, 1.f, 1.f));
            if (fill_brush) {
                const D2D1_ELLIPSE dot{ D2D1::Point2F(cx, cy), kRadioCircleSize * 0.22f, kRadioCircleSize * 0.22f };
                target->FillEllipse(dot, fill_brush);
                release_brush(fill_brush);
            }
        }
        IDWriteTextFormat* format = format_for_text(state, NodeType::Label, rule);
        if (format) {
            const D2D1_COLOR_F color = rule.has_color ? rule.color : D2D1::ColorF(0.12f, 0.12f, 0.12f, 1.f);
            ID2D1SolidColorBrush* text_brush = make_brush(target, color);
            if (text_brush) {
                D2D1_RECT_F text_rect{
                    bounds.left + kRadioCircleSize + kRadioLabelGap,
                    bounds.top,
                    bounds.right,
                    bounds.bottom,
                };
                const std::wstring text = to_wide(node.text);
                target->DrawText(text.c_str(), static_cast<UINT32>(text.size()), format, text_rect, text_brush);
                release_brush(text_brush);
            }
        }
    }

    if ((node.type == NodeType::Input || node.type == NodeType::TextArea) && visible) {
        const bool focused = node.input_index >= 0 && node.input_index == state.focused_input;
        const D2D1_COLOR_F bg = rule.has_bg ? rule.bg_color : D2D1::ColorF(1.f, 1.f, 1.f, 1.f);
        const D2D1_COLOR_F border = focused && rule.has_focus_border
            ? rule.focus_border_color
            : (rule.has_border ? rule.border_color : D2D1::ColorF(0.784f, 0.784f, 0.784f, 1.f));
        const float radius = rule.radius > 0.f ? rule.radius : kInputDefaultRadius;

        ID2D1SolidColorBrush* bg_brush = make_brush(target, bg);
        if (bg_brush) {
            draw_rounded_rect(target, bounds, radius, bg_brush);
            release_brush(bg_brush);
        }

        ID2D1SolidColorBrush* border_brush = make_brush(target, border);
        if (border_brush) {
            const D2D1_ROUNDED_RECT round{ bounds, radius, radius };
            target->DrawRoundedRectangle(round, border_brush, kInputBorderWidth);
            release_brush(border_brush);
        }
    }

    if (node.type == NodeType::Range && visible) {
        const D2D1_COLOR_F bg = rule.has_bg ? rule.bg_color : D2D1::ColorF(1.f, 1.f, 1.f, 1.f);
        const D2D1_COLOR_F border = rule.has_border ? rule.border_color : D2D1::ColorF(0.784f, 0.784f, 0.784f, 1.f);
        const float radius = rule.radius > 0.f ? rule.radius : kInputDefaultRadius;
        ID2D1SolidColorBrush* bg_brush = make_brush(target, bg);
        if (bg_brush) {
            draw_rounded_rect(target, bounds, radius, bg_brush);
            release_brush(bg_brush);
        }
        ID2D1SolidColorBrush* border_brush = make_brush(target, border);
        if (border_brush) {
            const D2D1_ROUNDED_RECT round{ bounds, radius, radius };
            target->DrawRoundedRectangle(round, border_brush, kInputBorderWidth);
            release_brush(border_brush);
        }

        const float arrow_left = bounds.right - kRangeArrowWidth;
        ID2D1SolidColorBrush* divider_brush = make_brush(target, border);
        if (divider_brush) {
            target->DrawLine(
                D2D1::Point2F(arrow_left, bounds.top + 2.f),
                D2D1::Point2F(arrow_left, bounds.bottom - 2.f),
                divider_brush,
                1.f
            );
            release_brush(divider_brush);
        }

        int range_index = -1;
        for (size_t i = 0; i < state.ranges.size(); ++i) {
            if (state.ranges[i].id == node.id) {
                range_index = static_cast<int>(i);
                break;
            }
        }
        const bool hover_up = range_index >= 0 && state.hovered_range == range_index && state.hovered_range_part == 0;
        const bool hover_down = range_index >= 0 && state.hovered_range == range_index && state.hovered_range_part == 1;
        const D2D1_RECT_F up_rect{
            arrow_left,
            bounds.top,
            bounds.right,
            bounds.top + (bounds.bottom - bounds.top) * 0.5f,
        };
        const D2D1_RECT_F down_rect{
            arrow_left,
            bounds.top + (bounds.bottom - bounds.top) * 0.5f,
            bounds.right,
            bounds.bottom,
        };
        if (hover_up) {
            ID2D1SolidColorBrush* hover_brush = make_brush(target, D2D1::ColorF(0.92f, 0.94f, 0.96f, 1.f));
            if (hover_brush) {
                draw_rounded_rect(target, up_rect, 0.f, hover_brush);
                release_brush(hover_brush);
            }
        }
        if (hover_down) {
            ID2D1SolidColorBrush* hover_brush = make_brush(target, D2D1::ColorF(0.92f, 0.94f, 0.96f, 1.f));
            if (hover_brush) {
                draw_rounded_rect(target, down_rect, 0.f, hover_brush);
                release_brush(hover_brush);
            }
        }

        IDWriteTextFormat* format = format_for_text(state, NodeType::Input, rule);
        if (format) {
            format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            ID2D1SolidColorBrush* arrow_brush = make_brush(target, D2D1::ColorF(0.2f, 0.2f, 0.2f, 1.f));
            if (arrow_brush) {
                target->DrawText(L"\u25B2", 1, format, up_rect, arrow_brush);
                target->DrawText(L"\u25BC", 1, format, down_rect, arrow_brush);
                release_brush(arrow_brush);
            }
            format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        }
    }

    if (node.type == NodeType::Image && visible) {
        const bool is_gallery_thumb = node.parent && node.parent->type == NodeType::Gallery;
        const bool hovered = !is_gallery_thumb && node.button_index >= 0 && node.button_index == hovered_image;
        ID2D1Bitmap* bitmap = bitmap_for_image(state, target, node.image_src);
        if (bitmap) {
            draw_image_cover(target, bitmap, bounds);
        } else {
            ID2D1SolidColorBrush* placeholder = make_brush(target, D2D1::ColorF(0.9f, 0.9f, 0.9f, 1.f));
            if (placeholder) {
                draw_rounded_rect(target, bounds, rule.radius > 0.f ? rule.radius : 8.f, placeholder);
                release_brush(placeholder);
            }
            if (!node.image_src.empty() && state.image_loading.find(node.image_src) != state.image_loading.end()) {
                const float cx = (bounds.left + bounds.right) * 0.5f;
                const float cy = (bounds.top + bounds.bottom) * 0.5f;
                paint_arc_spinner(
                    target,
                    state.factory,
                    cx,
                    cy,
                    10.f,
                    state.image_load_angle,
                    D2D1::ColorF(0.35f, 0.75f, 1.f, 1.f)
                );
            }
        }
        if (!is_gallery_thumb && (hovered || rule.has_border)) {
            const D2D1_COLOR_F border_color = hovered
                ? D2D1::ColorF(0.35f, 0.75f, 1.f, 1.f)
                : rule.border_color;
            ID2D1SolidColorBrush* border_brush = make_brush(target, border_color);
            if (border_brush) {
                const float stroke = hovered ? 2.f : 1.f;
                D2D1_ROUNDED_RECT round{
                    bounds,
                    rule.radius > 0.f ? rule.radius : 8.f,
                    rule.radius > 0.f ? rule.radius : 8.f,
                };
                target->DrawRoundedRectangle(round, border_brush, stroke);
                release_brush(border_brush);
            }
        }
        if (is_gallery_thumb) {
            const float pad = 10.f;
            const float radius = kRadioCircleSize * 0.5f;
            const float cx = bounds.left + pad + radius;
            const float cy = bounds.top + pad + radius;
            const D2D1_ELLIPSE outer = D2D1::Ellipse(D2D1::Point2F(cx, cy), radius + 2.f, radius + 2.f);
            ID2D1SolidColorBrush* backdrop = make_brush(target, D2D1::ColorF(1.f, 1.f, 1.f, 0.82f));
            if (backdrop) {
                target->FillEllipse(outer, backdrop);
                release_brush(backdrop);
            }
            const D2D1_COLOR_F accent = D2D1::ColorF(0.35f, 0.75f, 1.f, 1.f);
            const D2D1_COLOR_F ring = node.selected ? accent : D2D1::ColorF(0.45f, 0.45f, 0.45f, 1.f);
            const D2D1_ELLIPSE circle = D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius);
            ID2D1SolidColorBrush* ring_brush = make_brush(target, ring);
            if (ring_brush) {
                target->DrawEllipse(circle, ring_brush, 2.f);
                release_brush(ring_brush);
            }
            if (node.selected) {
                ID2D1SolidColorBrush* dot = make_brush(target, accent);
                if (dot) {
                    target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius * 0.42f, radius * 0.42f), dot);
                    release_brush(dot);
                }
            }
        }
        if (!node.text.empty()) {
            IDWriteTextFormat* format = format_for_text(state, NodeType::Text, rule);
            if (format) {
                ID2D1SolidColorBrush* text_brush = make_brush(target, D2D1::ColorF(0.12f, 0.12f, 0.12f, 1.f));
                if (text_brush) {
                    D2D1_RECT_F label_rect{
                        bounds.left + 8.f,
                        bounds.bottom - 22.f,
                        bounds.right - 8.f,
                        bounds.bottom - 4.f,
                    };
                    const std::wstring label = to_wide(node.text);
                    target->DrawText(label.c_str(), static_cast<UINT32>(label.size()), format, label_rect, text_brush);
                    release_brush(text_brush);
                }
            }
        }
    }

    for (const auto& child : node.children) {
        paint_node(state, *child, hovered_button, hovered_image);
    }
}

void destroy_inputs(XmlUiState& state) {
    for (auto& range : state.ranges) {
        if (range.edit_hwnd) DestroyWindow(range.edit_hwnd);
    }
    state.ranges.clear();
    for (auto& select : state.selects) {
        if (select.hwnd) DestroyWindow(select.hwnd);
    }
    state.selects.clear();
    for (auto& slider : state.sliders) {
        if (slider.hwnd) DestroyWindow(slider.hwnd);
    }
    state.sliders.clear();
    for (auto& input : state.inputs) {
        if (input.hwnd) {
            RemoveWindowSubclass(input.hwnd, input_edit_subclass, 0);
            DestroyWindow(input.hwnd);
        }
    }
    state.inputs.clear();
    state.focused_input = -1;
    state.hovered_range = -1;
    state.hovered_range_part = -1;
}

void position_native_control(HWND hwnd, const D2D1_RECT_F& bounds, float scroll_y, float viewport_height, bool use_full_bounds) {
    if (!hwnd) return;
    const bool visible = rect_intersects_viewport(bounds, scroll_y, viewport_height);
    const int x = static_cast<int>(std::floor(bounds.left));
    const int y = static_cast<int>(std::floor(bounds.top - scroll_y));
    const int w = static_cast<int>(std::ceil(bounds.right - bounds.left));
    const int h = static_cast<int>(std::ceil(bounds.bottom - bounds.top));
    if (w > 0 && h > 0 && visible) {
        SetWindowPos(hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
        ShowWindow(hwnd, SW_SHOW);
    } else {
        ShowWindow(hwnd, SW_HIDE);
    }
    (void)use_full_bounds;
}

void sync_native_positions(XmlUiState& state) {
    for (auto& input : state.inputs) {
        const D2D1_RECT_F inner = input_inner_bounds(input.bounds, state.input_insets);
        position_native_control(input.hwnd, inner, state.scroll_y, state.viewport_height, false);
    }
    for (auto& range : state.ranges) {
        const D2D1_RECT_F inner = range_edit_inner_bounds(range.bounds, state.input_insets);
        position_native_control(range.edit_hwnd, inner, state.scroll_y, state.viewport_height, false);
    }
    for (auto& select : state.selects) {
        position_native_control(select.hwnd, select.bounds, state.scroll_y, state.viewport_height, true);
    }
    for (auto& slider : state.sliders) {
        position_native_control(slider.hwnd, slider.bounds, state.scroll_y, state.viewport_height, true);
    }
}

void create_inputs(XmlUiState& state, HWND parent) {
    destroy_inputs(state);
    if (!state.root) return;

    static bool trackbar_ready = []() {
        INITCOMMONCONTROLSEX init{};
        init.dwSize = sizeof(init);
        init.dwICC = ICC_BAR_CLASSES;
        return InitCommonControlsEx(&init) != FALSE;
    }();

    std::vector<UiNode*> stack;
    stack.push_back(state.root.get());
    while (!stack.empty()) {
        UiNode& node = *stack.back();
        stack.pop_back();

        if (node.type == NodeType::Input || node.type == NodeType::TextArea) {
            node.input_index = -1;
            const StyleRule& rule = resolve_rule(state, node.class_name);
            InputControl control{};
            control.id = node.id;
            control.bounds = node.bounds;
            const D2D1_RECT_F inner = input_inner_bounds(node.bounds, state.input_insets);
            const float edit_left = inner.left;
            const float edit_right = inner.right;
            const int x = static_cast<int>(std::floor(edit_left));
            const int y = static_cast<int>(std::floor(inner.top - state.scroll_y));
            const int w = static_cast<int>(std::ceil(edit_right - edit_left));
            const int h = static_cast<int>(std::ceil(inner.bottom - inner.top));
            const bool visible = rect_intersects_viewport(node.bounds, state.scroll_y, state.viewport_height);
            if (w > 0 && h > 0) {
                DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP;
                if (node.type == NodeType::TextArea) {
                    style |= ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL;
                } else {
                    style |= ES_AUTOHSCROLL;
                    if (node.input_type == "password") style |= ES_PASSWORD;
                }
                control.hwnd = CreateWindowExW(
                    0,
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
                    SetWindowSubclass(control.hwnd, input_edit_subclass, 0, 0);
                    const int weight = rule.font_weight >= 600 ? FW_SEMIBOLD : FW_NORMAL;
                    const float font_size = effective_font_size(state, NodeType::Input, rule);
                    const std::wstring& family = state.typography.font_family.empty() ? L"Segoe UI" : state.typography.font_family;
                    HFONT font = CreateFontW(
                        -static_cast<int>(font_size),
                        0, 0, 0, weight, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, family.c_str()
                    );
                    SendMessageW(control.hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                    if (!node.input_value.empty()) {
                        SetWindowTextW(control.hwnd, to_wide(node.input_value).c_str());
                    }
                    ShowWindow(control.hwnd, visible ? SW_SHOW : SW_HIDE);
                }
                node.input_index = static_cast<int>(state.inputs.size());
                state.inputs.push_back(control);
            }
        }
        if (node.type == NodeType::Slider && trackbar_ready) {
            const StyleRule& rule = resolve_rule(state, node.class_name);
            const bool visible = rect_intersects_viewport(node.bounds, state.scroll_y, state.viewport_height);
            const int x = static_cast<int>(std::floor(node.bounds.left));
            const int y = static_cast<int>(std::floor(node.bounds.top - state.scroll_y));
            const int w = static_cast<int>(std::ceil(node.bounds.right - node.bounds.left));
            const int h = static_cast<int>(std::ceil(node.bounds.bottom - node.bounds.top));
            if (w > 0 && h > 0) {
                SliderControl control{};
                control.id = node.id;
                control.bounds = node.bounds;
                control.hwnd = CreateWindowExW(
                    0,
                    TRACKBAR_CLASSW,
                    L"",
                    WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
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
                    const int min_v = node.slider_min;
                    const int max_v = node.slider_max > min_v ? node.slider_max : min_v + 20;
                    SendMessageW(control.hwnd, TBM_SETRANGE, TRUE, MAKELPARAM(min_v, max_v));
                    SendMessageW(control.hwnd, TBM_SETPOS, TRUE, node.slider_value);
                    SendMessageW(control.hwnd, TBM_SETTICFREQ, 1, 0);
                    ShowWindow(control.hwnd, visible ? SW_SHOW : SW_HIDE);
                }
                state.sliders.push_back(control);
            }
        }
        if (node.type == NodeType::Select) {
            const StyleRule& rule = resolve_rule(state, node.class_name);
            const bool visible = rect_intersects_viewport(node.bounds, state.scroll_y, state.viewport_height);
            const int x = static_cast<int>(std::floor(node.bounds.left));
            const int y = static_cast<int>(std::floor(node.bounds.top - state.scroll_y));
            const int w = static_cast<int>(std::ceil(node.bounds.right - node.bounds.left));
            const int h = static_cast<int>(std::ceil(node.bounds.bottom - node.bounds.top));
            if (w > 0 && h > 0) {
                SelectControl control{};
                control.id = node.id;
                control.bounds = node.bounds;
                control.hwnd = CreateWindowExW(
                    0,
                    WC_COMBOBOXW,
                    L"",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                    x,
                    y,
                    w,
                    h + 200,
                    parent,
                    nullptr,
                    GetModuleHandleW(nullptr),
                    nullptr
                );
                if (control.hwnd) {
                    if (node.select_fonts) {
                        const std::vector<std::wstring>& font_list = system_fonts();
                        for (const auto& font : font_list) {
                            SendMessageW(control.hwnd, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(font.c_str()));
                        }
                        const std::wstring& preferred = state.typography.font_family.empty() ? L"Segoe UI" : state.typography.font_family;
                        int selected = 0;
                        for (size_t i = 0; i < font_list.size(); ++i) {
                            if (_wcsicmp(font_list[i].c_str(), preferred.c_str()) == 0) {
                                selected = static_cast<int>(i);
                                break;
                            }
                        }
                        SendMessageW(control.hwnd, CB_SETCURSEL, selected, 0);
                    } else {
                        int selected = 0;
                        for (size_t i = 0; i < node.select_options.size(); ++i) {
                            const std::wstring option = to_wide(node.select_options[i]);
                            SendMessageW(control.hwnd, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(option.c_str()));
                            if (!node.selected_value.empty() && node.select_options[i] == node.selected_value) {
                                selected = static_cast<int>(i);
                            }
                        }
                        if (!node.select_options.empty()) {
                            SendMessageW(control.hwnd, CB_SETCURSEL, selected, 0);
                        }
                    }
                    const float font_size = effective_font_size(state, NodeType::Input, rule);
                    const std::wstring& family = state.typography.font_family.empty() ? L"Segoe UI" : state.typography.font_family;
                    HFONT font = CreateFontW(
                        -static_cast<int>(font_size),
                        0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, family.c_str()
                    );
                    SendMessageW(control.hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                    ShowWindow(control.hwnd, visible ? SW_SHOW : SW_HIDE);
                }
                state.selects.push_back(control);
            }
        }
        if (node.type == NodeType::Range) {
            const StyleRule& rule = resolve_rule(state, node.class_name);
            const bool visible = rect_intersects_viewport(node.bounds, state.scroll_y, state.viewport_height);
            RangeControl control{};
            control.id = node.id;
            control.bounds = node.bounds;
            control.min_value = node.slider_min;
            control.max_value = node.slider_max > node.slider_min ? node.slider_max : node.slider_min + 100;
            const float mid_y = node.bounds.top + (node.bounds.bottom - node.bounds.top) * 0.5f;
            control.up_bounds = D2D1::RectF(
                node.bounds.right - kRangeArrowWidth,
                node.bounds.top,
                node.bounds.right,
                mid_y
            );
            control.down_bounds = D2D1::RectF(
                node.bounds.right - kRangeArrowWidth,
                mid_y,
                node.bounds.right,
                node.bounds.bottom
            );

            const D2D1_RECT_F inner = range_edit_inner_bounds(node.bounds, state.input_insets);
            const int x = static_cast<int>(std::floor(inner.left));
            const int y = static_cast<int>(std::floor(inner.top - state.scroll_y));
            const int w = static_cast<int>(std::ceil(inner.right - inner.left));
            const int h = static_cast<int>(std::ceil(inner.bottom - inner.top));
            if (w > 0 && h > 0) {
                control.edit_hwnd = CreateWindowExW(
                    0,
                    L"EDIT",
                    L"",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
                    x,
                    y,
                    w,
                    h,
                    parent,
                    nullptr,
                    GetModuleHandleW(nullptr),
                    nullptr
                );
                if (control.edit_hwnd) {
                    const float font_size = effective_font_size(state, NodeType::Input, rule);
                    const std::wstring& family = state.typography.font_family.empty() ? L"Segoe UI" : state.typography.font_family;
                    HFONT font = CreateFontW(
                        -static_cast<int>(font_size),
                        0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, family.c_str()
                    );
                    SendMessageW(control.edit_hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                    set_range_edit_value(state, control, node.slider_value);
                    ShowWindow(control.edit_hwnd, visible ? SW_SHOW : SW_HIDE);
                }
                state.ranges.push_back(control);
            }
        }
        if (node.type == NodeType::Button || node.type == NodeType::SmallButton || node.type == NodeType::Radio) {
            node.button_index = static_cast<int>(state.buttons.size());
            state.buttons.push_back(&node);
        }
        if (node.type == NodeType::Image) {
            node.button_index = static_cast<int>(state.images.size());
            state.images.push_back(&node);
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

void raise_combo_dropdown(HWND combo) {
    if (!combo) return;
    SetWindowPos(combo, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    COMBOBOXINFO info{};
    info.cbSize = sizeof(info);
    if (GetComboBoxInfo(combo, &info) && info.hwndList) {
        SetWindowPos(
            info.hwndList,
            HWND_TOPMOST,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW
        );
    }
}

void lower_combo_dropdown(HWND combo) {
    if (!combo) return;
    COMBOBOXINFO info{};
    info.cbSize = sizeof(info);
    if (GetComboBoxInfo(combo, &info) && info.hwndList) {
        SetWindowPos(
            info.hwndList,
            HWND_NOTOPMOST,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE
        );
    }
}

void paint_arc_spinner(
    ID2D1HwndRenderTarget* target,
    ID2D1Factory* factory,
    float cx,
    float cy,
    float radius,
    float angle_deg,
    D2D1_COLOR_F color,
    float stroke
) {
    if (!target || !factory) return;

    constexpr float kPi = 3.14159265f;
    constexpr float kSweepDeg = 180.f * 0.65f;
    const float sweep_rad = kSweepDeg * kPi / 180.f;
    const float base_start_rad = -kPi * 0.5f;

    const float x0 = cx + radius * std::cos(base_start_rad);
    const float y0 = cy + radius * std::sin(base_start_rad);
    const float x1 = cx + radius * std::cos(base_start_rad + sweep_rad);
    const float y1 = cy + radius * std::sin(base_start_rad + sweep_rad);

    ID2D1PathGeometry* path = nullptr;
    if (FAILED(factory->CreatePathGeometry(&path)) || !path) return;
    ID2D1GeometrySink* sink = nullptr;
    if (FAILED(path->Open(&sink)) || !sink) {
        path->Release();
        return;
    }
    sink->BeginFigure(D2D1::Point2F(x0, y0), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddArc(D2D1::ArcSegment(
        D2D1::Point2F(x1, y1),
        D2D1::SizeF(radius, radius),
        0.f,
        D2D1_SWEEP_DIRECTION_CLOCKWISE,
        D2D1_ARC_SIZE_SMALL
    ));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    sink->Release();

    D2D1_MATRIX_3X2_F prior{};
    target->GetTransform(&prior);
    const D2D1_MATRIX_3X2_F rot = D2D1::Matrix3x2F::Rotation(angle_deg, D2D1::Point2F(cx, cy));
    target->SetTransform(rot * prior);

    ID2D1SolidColorBrush* brush = make_brush(target, color);
    if (brush) {
        target->DrawGeometry(path, brush, stroke);
        release_brush(brush);
    }

    target->SetTransform(prior);
    path->Release();
}

void paint_busy_indicator(XmlUiState& state, ID2D1HwndRenderTarget* target, float angle_deg, const std::string& near_button_id) {
    if (!state.root || near_button_id.empty() || !target || !state.factory) return;
    UiNode* btn = find_node_by_id(state.root.get(), near_button_id);
    if (!btn) return;
    const D2D1_RECT_F bounds = scroll_rect(btn->bounds, state.scroll_y);
    const float cx = bounds.right + 18.f;
    const float cy = (bounds.top + bounds.bottom) * 0.5f;
    paint_arc_spinner(target, state.factory, cx, cy, 7.f, angle_deg, D2D1::ColorF(0.35f, 0.75f, 1.f, 1.f));
}

int hit_test_button(const XmlUiState& state, POINT content_pt) {
    for (const UiNode* button : state.buttons) {
        if (!button || button->disabled) continue;
        if (point_in_bounds(content_pt, button->bounds)) return button->button_index;
    }
    return -1;
}

int hit_test_image(const XmlUiState& state, POINT content_pt) {
    for (const UiNode* image : state.images) {
        if (image && point_in_bounds(content_pt, image->bounds)) return image->button_index;
    }
    return -1;
}

bool hit_test_range_part(const XmlUiState& state, POINT content_pt, int& out_index, int& out_part) {
    for (size_t i = 0; i < state.ranges.size(); ++i) {
        const auto& range = state.ranges[i];
        if (point_in_bounds(content_pt, range.up_bounds)) {
            out_index = static_cast<int>(i);
            out_part = 0;
            return true;
        }
        if (point_in_bounds(content_pt, range.down_bounds)) {
            out_index = static_cast<int>(i);
            out_part = 1;
            return true;
        }
    }
    return false;
}

bool hit_test_input(const XmlUiState& state, POINT pt) {
    for (const auto& input : state.inputs) {
        if (input.hwnd && point_in_bounds(pt, input.bounds)) return true;
    }
    return false;
}

UiNode* find_node_by_id(UiNode* node, const std::string& id) {
    if (!node) return nullptr;
    if (node->id == id) return node;
    for (auto& child : node->children) {
        if (UiNode* found = find_node_by_id(child.get(), id)) return found;
    }
    return nullptr;
}

void set_image_selection(UiNode* node, const std::string& selected_id) {
    if (!node) return;
    if (node->type == NodeType::Image) {
        node->selected = (node->id == selected_id);
    }
    for (auto& child : node->children) {
        set_image_selection(child.get(), selected_id);
    }
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
            UiNode* raw = node.get();
            if (token.name == "Layout") {
                const auto width_it = token.attrs.find("width");
                if (width_it != token.attrs.end()) {
                    state.layout_width = parse_float(width_it->second, state.layout_width);
                }
            }
            if (token.name == "Gallery") {
                const auto columns_it = token.attrs.find("columns");
                if (columns_it != token.attrs.end()) {
                    raw->gallery_columns = static_cast<int>(parse_float(columns_it->second, static_cast<float>(raw->gallery_columns)));
                }
            }

            if (stack.empty()) {
                state.root = std::move(node);
            } else {
                raw->parent = stack.back().node;
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
            UiNode* raw = node.get();
            if (stack.empty()) {
                state.root = std::move(node);
            } else if (stack.back().node) {
                raw->parent = stack.back().node;
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
    impl_->state.host_hwnd = hwnd_;

    layout(0, 0, static_cast<int>(impl_->state.layout_width), 100);
    impl_->state.paint_ready = true;
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

bool XmlUiHost::reload_xml(const char* xml) {
    if (!impl_ || !hwnd_ || !xml) return false;

    impl_->state.layout_in_progress = true;
    detail::sync_range_nodes_from_edits(impl_->state);
    detail::sync_select_nodes_from_combos(impl_->state);
    detail::sync_input_nodes_from_edits(impl_->state);
    detail::destroy_inputs(impl_->state);

    for (auto& pair : impl_->state.image_bitmaps) {
        if (pair.second) pair.second->Release();
    }
    impl_->state.image_bitmaps.clear();
    detail::clear_image_load_state(impl_->state, hwnd_);

    const float scroll_y = impl_->state.scroll_y;
    impl_->state.rules.clear();
    impl_->state.root.reset();
    impl_->state.buttons.clear();
    impl_->state.images.clear();

    if (!detail::parse_xml(impl_->state, xml)) {
        impl_->state.layout_in_progress = false;
        return false;
    }

    impl_->state.scroll_y = scroll_y;
    detail::layout_tree(impl_->state);
    detail::update_scroll_metrics(impl_->state);
    detail::create_inputs(impl_->state, hwnd_);
    impl_->state.layout_in_progress = false;
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

void XmlUiHost::destroy() {
  if (hwnd_) {
    KillTimer(hwnd_, detail::kBusyTimerId);
    KillTimer(hwnd_, detail::kImageLoadTimerId);
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
  if (impl_) {
    detail::clear_image_load_state(impl_->state, nullptr);
    impl_->state.host_hwnd = nullptr;
    detail::destroy_inputs(impl_->state);
    detail::destroy_render(impl_->state);
    if (impl_->state.factory) { impl_->state.factory->Release(); impl_->state.factory = nullptr; }
    if (impl_->state.write_factory) { impl_->state.write_factory->Release(); impl_->state.write_factory = nullptr; }
    delete impl_;
    impl_ = nullptr;
  }
  parent_ = nullptr;
}

void XmlUiHost::refresh_input_insets() {
    if (!impl_ || !hwnd_) return;
    impl_->state.input_insets = input_insets_;
    detail::sync_native_positions(impl_->state);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void XmlUiHost::set_scroll_wheel_step(float step) {
    scroll_wheel_step_ = step > 0.f ? step : 25.f;
    if (impl_) impl_->state.scroll_wheel_step = scroll_wheel_step_;
}

void XmlUiHost::layout(int x, int y, int width, int height) {
    if (!hwnd_ || !impl_) return;
    if (width <= 0 || height <= 0) return;
    impl_->state.layout_in_progress = true;
    SetWindowPos(hwnd_, HWND_TOP, x, y, width, height, SWP_NOACTIVATE);
    impl_->state.layout_width = static_cast<float>(width);
    impl_->state.input_insets = input_insets_;
    impl_->state.typography = typography_;
    impl_->state.control_width = control_width_;
    impl_->state.label_control_gap = label_control_gap_;
    impl_->state.scroll_wheel_step = scroll_wheel_step_;
    impl_->state.button_width = button_width_;
    impl_->state.small_button_width = small_button_width_;
    impl_->state.viewport_height = static_cast<float>(height);
    impl_->state.buttons.clear();
    impl_->state.images.clear();
    detail::sync_range_nodes_from_edits(impl_->state);
    detail::sync_select_nodes_from_combos(impl_->state);
    detail::sync_input_nodes_from_edits(impl_->state);
    detail::layout_tree(impl_->state);
    detail::update_scroll_metrics(impl_->state);
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
    case WM_NCHITTEST:
        return DefWindowProcW(hwnd, msg, wparam, lparam);

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
                state.viewport_height = static_cast<float>(height);
            }
        }
        state.buttons.clear();
        state.images.clear();
        sync_range_nodes_from_edits(state);
        sync_select_nodes_from_combos(state);
        sync_input_nodes_from_edits(state);
        layout_tree(state);
        update_scroll_metrics(state);
        destroy_inputs(state);
        create_inputs(state, hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_MOUSEWHEEL: {
        const float max_y = max_scroll_y(state);
        if (max_y <= 0.f) return 0;
        const short delta = GET_WHEEL_DELTA_WPARAM(wparam);
        const float step = state.scroll_wheel_step > 0.f ? state.scroll_wheel_step : 25.f;
        state.scroll_y -= static_cast<float>(delta) / kWheelNotchDelta * step;
        if (state.scroll_y < 0.f) state.scroll_y = 0.f;
        if (state.scroll_y > max_y) state.scroll_y = max_y;
        sync_native_positions(state);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_MOUSEMOVE: {
        POINT pt{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
        const POINT content_pt = content_point(pt, state.scroll_y);
        const int hovered = hit_test_button(state, content_pt);
        const int hovered_image = hit_test_image(state, content_pt);
        int hovered_range = -1;
        int hovered_range_part = -1;
        hit_test_range_part(state, content_pt, hovered_range, hovered_range_part);
        if (hovered != state.hovered_button || hovered_image != state.hovered_image ||
            hovered_range != state.hovered_range || hovered_range_part != state.hovered_range_part) {
            state.hovered_button = hovered;
            state.hovered_image = hovered_image;
            state.hovered_range = hovered_range;
            state.hovered_range_part = hovered_range_part;
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
        if (state.hovered_button != -1 || state.hovered_image != -1 ||
            state.hovered_range != -1) {
            state.hovered_button = -1;
            state.hovered_image = -1;
            state.hovered_range = -1;
            state.hovered_range_part = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_LBUTTONDOWN: {
        POINT pt{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
        const POINT content_pt = content_point(pt, state.scroll_y);
        for (const auto& input : state.inputs) {
            if (!input.hwnd) continue;
            if (point_in_bounds(content_pt, input.bounds)) {
                SetFocus(input.hwnd);
                break;
            }
        }
        for (const auto& range : state.ranges) {
            if (!range.edit_hwnd) continue;
            if (point_in_bounds(content_pt, range.bounds) && !point_in_bounds(content_pt, range.up_bounds) &&
                !point_in_bounds(content_pt, range.down_bounds)) {
                SetFocus(range.edit_hwnd);
                break;
            }
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    case WM_LBUTTONUP: {
        POINT pt{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
        const POINT content_pt = content_point(pt, state.scroll_y);
        int range_index = -1;
        int range_part = -1;
        if (hit_test_range_part(state, content_pt, range_index, range_part) &&
            range_index >= 0 && range_index < static_cast<int>(state.ranges.size())) {
            RangeControl& range = state.ranges[static_cast<size_t>(range_index)];
            wchar_t buffer[32]{};
            GetWindowTextW(range.edit_hwnd, buffer, 32);
            int value = _wtoi(buffer);
            value += range_part == 0 ? 1 : -1;
            value = clamp_range_value(value, range.min_value, range.max_value);
            set_range_edit_value(state, range, value);
            if (impl->owner && impl->owner->range_handler_fn_) {
                impl->owner->range_handler_fn_(
                    impl->owner->range_handler_ctx_,
                    to_wide(range.id),
                    value
                );
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        const int clicked = hit_test_button(state, content_pt);
        if (clicked >= 0 && impl->owner) {
            for (UiNode* button : state.buttons) {
                if (!button || button->button_index != clicked || button->disabled) continue;
                if (button->type == NodeType::Radio) {
                    UiNode* group = button->parent;
                    if (group && group->type == NodeType::RadioGroup && !button->option_value.empty()) {
                        group->selected_value = button->option_value;
                        if (impl->owner->radio_handler_fn_) {
                            impl->owner->radio_handler_fn_(
                                impl->owner->radio_handler_ctx_,
                                to_wide(group->id),
                                to_wide(button->option_value)
                            );
                        }
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                    return 0;
                }
                if (impl->owner->button_handler_fn_) {
                    impl->owner->button_handler_fn_(impl->owner->button_handler_ctx_, to_wide(button->id));
                }
                return 0;
            }
        }
        const int clicked_image = hit_test_image(state, content_pt);
        if (clicked_image >= 0 && impl->owner && impl->owner->image_handler_fn_) {
            for (UiNode* image : state.images) {
                if (image && image->button_index == clicked_image) {
                    impl->owner->image_handler_fn_(impl->owner->image_handler_ctx_, to_wide(image->id));
                    break;
                }
            }
        }
        return 0;
    }

    case WM_HSCROLL: {
        HWND track = reinterpret_cast<HWND>(lparam);
        for (const auto& slider : state.sliders) {
            if (slider.hwnd != track) continue;
            const int pos = static_cast<int>(SendMessageW(track, TBM_GETPOS, 0, 0));
            if (impl->owner && impl->owner->slider_handler_fn_) {
                impl->owner->slider_handler_fn_(
                    impl->owner->slider_handler_ctx_,
                    to_wide(slider.id),
                    pos
                );
            }
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    case WM_CTLCOLOREDIT: {
        HDC hdc = reinterpret_cast<HDC>(wparam);
        SetBkMode(hdc, OPAQUE);
        SetBkColor(hdc, RGB(255, 255, 255));
        SetTextColor(hdc, RGB(26, 26, 26));
        return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
    }

    case WM_TIMER:
        if (wparam == kBusyTimerId && impl->owner && impl->owner->busy_) {
            impl->owner->busy_angle_ += 24.f;
            if (impl->owner->busy_angle_ >= 360.f) impl->owner->busy_angle_ -= 360.f;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (wparam == kImageLoadTimerId && !state.image_loading.empty()) {
            state.image_load_angle += 24.f;
            if (state.image_load_angle >= 360.f) state.image_load_angle -= 360.f;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);

    case WM_COMMAND: {
        const UINT notify = HIWORD(wparam);
        if (notify == CBN_DROPDOWN) {
            raise_combo_dropdown(reinterpret_cast<HWND>(lparam));
            return 0;
        }
        if (notify == CBN_CLOSEUP) {
            lower_combo_dropdown(reinterpret_cast<HWND>(lparam));
            return 0;
        }
        if (notify == CBN_SELCHANGE) {
            HWND combo = reinterpret_cast<HWND>(lparam);
            for (const auto& select : state.selects) {
                if (select.hwnd != combo) continue;
                const int idx = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
                if (idx >= 0) {
                    wchar_t buffer[256]{};
                    SendMessageW(combo, CB_GETLBTEXT, static_cast<WPARAM>(idx), reinterpret_cast<LPARAM>(buffer));
                    if (state.root) {
                        if (UiNode* node = find_node_by_id(state.root.get(), select.id)) {
                            node->selected_value = wide_to_utf8(buffer);
                        }
                    }
                    if (impl->owner && impl->owner->select_handler_fn_) {
                        impl->owner->select_handler_fn_(
                            impl->owner->select_handler_ctx_,
                            to_wide(select.id),
                            buffer
                        );
                    }
                }
                return 0;
            }
        }
        if (notify == EN_CHANGE) {
            if (state.range_edit_syncing) return 0;
            HWND edit_hwnd = reinterpret_cast<HWND>(lparam);
            for (auto& range : state.ranges) {
                if (range.edit_hwnd != edit_hwnd) continue;
                wchar_t buffer[32]{};
                GetWindowTextW(edit_hwnd, buffer, 32);
                int value = _wtoi(buffer);
                value = clamp_range_value(value, range.min_value, range.max_value);
                set_range_edit_value(state, range, value);
                if (impl->owner && impl->owner->range_handler_fn_) {
                    impl->owner->range_handler_fn_(
                        impl->owner->range_handler_ctx_,
                        to_wide(range.id),
                        value
                    );
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (notify == EN_SETFOCUS || notify == EN_KILLFOCUS) {
            HWND edit_hwnd = reinterpret_cast<HWND>(lparam);
            int next_focused = -1;
            for (size_t i = 0; i < state.inputs.size(); ++i) {
                if (state.inputs[i].hwnd == edit_hwnd) {
                    next_focused = (notify == EN_SETFOCUS) ? static_cast<int>(i) : -1;
                    break;
                }
            }
            if (next_focused != state.focused_input) {
                state.focused_input = next_focused;
                InvalidateRect(hwnd, nullptr, FALSE);
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
                paint_node(state, *state.root, state.hovered_button, state.hovered_image);
                if (impl->owner && impl->owner->busy_) {
                    const std::string near_id(
                        impl->owner->busy_near_button_id_.begin(),
                        impl->owner->busy_near_button_id_.end()
                    );
                    paint_busy_indicator(state, state.target, impl->owner->busy_angle_, near_id);
                }
            }
            state.target->EndDraw();
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        clear_image_load_state(state, hwnd);
        detail::destroy_inputs(state);
        detail::destroy_render(state);
        return 0;

    case kMsgImageBitmapReady: {
        ImageLoadResult* result = reinterpret_cast<ImageLoadResult*>(lparam);
        if (!result) return 0;
        state.image_loading.erase(result->src);
        if (result->hbitmap) {
            if (ensure_render(state, hwnd) && state.target) {
                ID2D1Bitmap* bitmap = create_bitmap_from_hbitmap(state, state.target, result->hbitmap);
                if (bitmap) {
                    state.image_bitmaps[result->src] = bitmap;
                } else {
                    state.image_failed.insert(result->src);
                }
            }
            DeleteObject(result->hbitmap);
        } else {
            state.image_failed.insert(result->src);
        }
        delete result;
        if (state.image_loading.empty()) {
            KillTimer(hwnd, kImageLoadTimerId);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

std::wstring XmlUiHost::get_input_text(const std::wstring& id) const {
    if (!impl_ || id.empty()) return {};
    const std::string id_utf8(id.begin(), id.end());
    for (const auto& input : impl_->state.inputs) {
        if (input.id == id_utf8 && input.hwnd) {
            const int len = GetWindowTextLengthW(input.hwnd);
            if (len <= 0) return {};
            if (len > 65536) return {};
            std::wstring text(static_cast<size_t>(len) + 1u, L'\0');
            const int copied = GetWindowTextW(input.hwnd, text.data(), len + 1);
            if (copied <= 0) return {};
            text.resize(static_cast<size_t>(copied));
            return text;
        }
    }
    if (impl_->state.root) {
        if (const detail::UiNode* node = detail::find_node_by_id(impl_->state.root.get(), id_utf8)) {
            return detail::to_wide(node->input_value);
        }
    }
    return {};
}

void XmlUiHost::set_input_text(const std::wstring& id, const std::wstring& text) {
    if (!impl_ || id.empty()) return;
    const std::string id_utf8(id.begin(), id.end());
    const std::string value_utf8 = detail::wide_to_utf8(text);
    if (impl_->state.root) {
        if (detail::UiNode* node = detail::find_node_by_id(impl_->state.root.get(), id_utf8)) {
            node->input_value = value_utf8;
        }
    }
    for (const auto& input : impl_->state.inputs) {
        if (input.id == id_utf8 && input.hwnd) {
            SetWindowTextW(input.hwnd, text.c_str());
            return;
        }
    }
}

int XmlUiHost::get_slider_value(const std::wstring& id) const {
    if (!impl_ || id.empty()) return 0;
    const std::string id_utf8(id.begin(), id.end());
    for (const auto& slider : impl_->state.sliders) {
        if (slider.id == id_utf8 && slider.hwnd) {
            return static_cast<int>(SendMessageW(slider.hwnd, TBM_GETPOS, 0, 0));
        }
    }
    return 0;
}

void XmlUiHost::set_slider_value(const std::wstring& id, int value) {
    if (!impl_ || id.empty()) return;
    const std::string id_utf8(id.begin(), id.end());
    for (const auto& slider : impl_->state.sliders) {
        if (slider.id == id_utf8 && slider.hwnd) {
            SendMessageW(slider.hwnd, TBM_SETPOS, TRUE, value);
            return;
        }
    }
}

void XmlUiHost::set_text(const std::wstring& id, const std::wstring& text) {
    if (!impl_ || !impl_->state.root || id.empty()) return;
    const std::string id_utf8(id.begin(), id.end());
    detail::UiNode* node = detail::find_node_by_id(impl_->state.root.get(), id_utf8);
    if (!node) return;
    node->text.assign(text.begin(), text.end());
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void XmlUiHost::set_image_selected(const std::wstring& id) {
    if (!impl_ || !impl_->state.root || id.empty()) return;
    const std::string id_utf8(id.begin(), id.end());
    detail::set_image_selection(impl_->state.root.get(), id_utf8);
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void XmlUiHost::set_button_selected(const std::wstring& id, bool selected) {
    if (!impl_ || !impl_->state.root || id.empty()) return;
    const std::string id_utf8(id.begin(), id.end());
    detail::UiNode* node = detail::find_node_by_id(impl_->state.root.get(), id_utf8);
    if (!node || node->type != detail::NodeType::Button) return;
    node->selected = selected;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void XmlUiHost::set_button_enabled(const std::wstring& id, bool enabled) {
    if (!impl_ || !impl_->state.root || id.empty()) return;
    const std::string id_utf8(id.begin(), id.end());
    detail::UiNode* node = detail::find_node_by_id(impl_->state.root.get(), id_utf8);
    if (!node) return;
    if (node->type != detail::NodeType::Button && node->type != detail::NodeType::SmallButton) return;
    node->disabled = !enabled;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void XmlUiHost::set_busy(bool busy, const std::wstring& near_button_id) {
    busy_ = busy;
    busy_near_button_id_ = near_button_id;
    if (!hwnd_) return;
    if (busy) {
        busy_angle_ = 0.f;
        SetTimer(hwnd_, detail::kBusyTimerId, 50, nullptr);
    } else {
        KillTimer(hwnd_, detail::kBusyTimerId);
        busy_angle_ = 0.f;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void XmlUiHost::set_typography(UiTypography typography) {
    typography_ = std::move(typography);
    if (!impl_) return;
    detail::clear_text_formats(impl_->state);
    impl_->state.typography = typography_;
    if (!hwnd_) return;
    impl_->state.layout_in_progress = true;
    detail::sync_select_nodes_from_combos(impl_->state);
    detail::sync_input_nodes_from_edits(impl_->state);
    detail::destroy_inputs(impl_->state);
    detail::layout_tree(impl_->state);
    detail::update_scroll_metrics(impl_->state);
    detail::create_inputs(impl_->state, hwnd_);
    impl_->state.layout_in_progress = false;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

int XmlUiHost::get_range_value(const std::wstring& id) const {
    if (!impl_ || id.empty()) return 0;
    const std::string id_utf8(id.begin(), id.end());
    for (const auto& range : impl_->state.ranges) {
        if (range.id != id_utf8 || !range.edit_hwnd) continue;
        wchar_t buffer[32]{};
        GetWindowTextW(range.edit_hwnd, buffer, 32);
        return _wtoi(buffer);
    }
    return 0;
}

void XmlUiHost::set_range_value(const std::wstring& id, int value) {
    if (!impl_ || id.empty()) return;
    const std::string id_utf8(id.begin(), id.end());
    detail::UiNode* node = detail::find_node_by_id(impl_->state.root.get(), id_utf8);
    if (node) {
        const int max_value = node->slider_max > node->slider_min ? node->slider_max : node->slider_min + 100;
        value = detail::clamp_range_value(value, node->slider_min, max_value);
        node->slider_value = value;
    }
    for (auto& range : impl_->state.ranges) {
        if (range.id != id_utf8) continue;
        detail::set_range_edit_value(impl_->state, range, value);
        return;
    }
}

std::wstring XmlUiHost::get_select_value(const std::wstring& id) const {
    if (!impl_ || id.empty()) return {};
    const std::string id_utf8(id.begin(), id.end());
    for (const auto& select : impl_->state.selects) {
        if (select.id != id_utf8 || !select.hwnd) continue;
        const int idx = static_cast<int>(SendMessageW(select.hwnd, CB_GETCURSEL, 0, 0));
        if (idx < 0) break;
        wchar_t buffer[256]{};
        SendMessageW(select.hwnd, CB_GETLBTEXT, static_cast<WPARAM>(idx), reinterpret_cast<LPARAM>(buffer));
        return buffer;
    }
    if (impl_->state.root) {
        if (const detail::UiNode* node = detail::find_node_by_id(impl_->state.root.get(), id_utf8)) {
            return detail::to_wide(node->selected_value);
        }
    }
    return {};
}

void XmlUiHost::set_select_value(const std::wstring& id, const std::wstring& value) {
    if (!impl_ || id.empty() || value.empty()) return;
    const std::string id_utf8(id.begin(), id.end());
    const std::string value_utf8 = detail::wide_to_utf8(value);
    if (impl_->state.root) {
        if (detail::UiNode* node = detail::find_node_by_id(impl_->state.root.get(), id_utf8)) {
            node->selected_value = value_utf8;
        }
    }
    for (const auto& select : impl_->state.selects) {
        if (select.id != id_utf8 || !select.hwnd) continue;
        const int count = static_cast<int>(SendMessageW(select.hwnd, CB_GETCOUNT, 0, 0));
        for (int i = 0; i < count; ++i) {
            wchar_t buffer[256]{};
            SendMessageW(select.hwnd, CB_GETLBTEXT, static_cast<WPARAM>(i), reinterpret_cast<LPARAM>(buffer));
            if (_wcsicmp(buffer, value.c_str()) == 0) {
                SendMessageW(select.hwnd, CB_SETCURSEL, i, 0);
                return;
            }
        }
        SendMessageW(select.hwnd, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value.c_str()));
        SendMessageW(select.hwnd, CB_SETCURSEL, count, 0);
        return;
    }
}

void XmlUiHost::set_select_options(const std::wstring& id, const std::vector<std::wstring>& options, const std::wstring& selected) {
    if (!impl_ || id.empty() || options.empty()) return;
    const std::string id_utf8(id.begin(), id.end());
    std::wstring pick = selected;
    if (pick.empty()) pick = options.front();

    if (impl_->state.root) {
        if (detail::UiNode* node = detail::find_node_by_id(impl_->state.root.get(), id_utf8)) {
            node->select_options.clear();
            for (const auto& option : options) {
                node->select_options.push_back(detail::wide_to_utf8(option));
            }
            node->selected_value = detail::wide_to_utf8(pick);
        }
    }

    for (const auto& select : impl_->state.selects) {
        if (select.id != id_utf8 || !select.hwnd) continue;
        SendMessageW(select.hwnd, CB_RESETCONTENT, 0, 0);
        int selected_idx = 0;
        for (size_t i = 0; i < options.size(); ++i) {
            SendMessageW(select.hwnd, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(options[i].c_str()));
            if (_wcsicmp(options[i].c_str(), pick.c_str()) == 0) {
                selected_idx = static_cast<int>(i);
            }
        }
        SendMessageW(select.hwnd, CB_SETCURSEL, selected_idx, 0);
        return;
    }
}

std::wstring XmlUiHost::get_radio_value(const std::wstring& id) const {
    if (!impl_ || !impl_->state.root || id.empty()) return {};
    const std::string id_utf8(id.begin(), id.end());
    detail::UiNode* node = detail::find_node_by_id(impl_->state.root.get(), id_utf8);
    if (!node || node->type != detail::NodeType::RadioGroup) return {};
    return detail::to_wide(node->selected_value);
}

void XmlUiHost::set_radio_value(const std::wstring& id, const std::wstring& value) {
    if (!impl_ || !impl_->state.root || id.empty()) return;
    const std::string id_utf8(id.begin(), id.end());
    const std::string value_utf8(value.begin(), value.end());
    detail::UiNode* node = detail::find_node_by_id(impl_->state.root.get(), id_utf8);
    if (!node || node->type != detail::NodeType::RadioGroup) return;
    if (!value_utf8.empty()) {
        node->selected_value = value_utf8;
    } else if (node->selected_value.empty()) {
        for (const auto& child : node->children) {
            if (child && child->type == detail::NodeType::Radio && !child->option_value.empty()) {
                node->selected_value = child->option_value;
                break;
            }
        }
    }
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

} // namespace ogg::ui

#endif
