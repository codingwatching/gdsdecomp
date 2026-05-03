#include "gdre_xml_highlighter.h"

#include "core/object/class_db.h"
#include "core/string/char_utils.h"
#include "scene/gui/text_edit.h"

static _FORCE_INLINE_ bool _is_xml_name_start(char32_t c) {
	return is_ascii_alphabet_char(c) || c == '_' || c == ':';
}

static _FORCE_INLINE_ bool _is_xml_name(char32_t c) {
	return is_ascii_alphanumeric_char(c) || c == '_' || c == ':' || c == '-' || c == '.';
}

GDREXMLHighlighter::State GDREXMLHighlighter::_get_start_state(int p_line) {
	if (p_line <= 0) {
		return STATE_TEXT;
	}
	int prev_line = p_line - 1;
	while (prev_line > 0 && !line_end_state_cache.has(prev_line)) {
		prev_line--;
	}
	for (int i = prev_line; i < p_line - 1; i++) {
		get_line_syntax_highlighting(i);
	}
	if (!line_end_state_cache.has(p_line - 1)) {
		get_line_syntax_highlighting(p_line - 1);
	}
	HashMap<int, State>::ConstIterator E = line_end_state_cache.find(p_line - 1);
	if (E) {
		return E->value;
	}
	return STATE_TEXT;
}

Dictionary GDREXMLHighlighter::_get_line_syntax_highlighting_impl(int p_line) {
	Dictionary color_map;

	State state = _get_start_state(p_line);

	const String &str = text_edit->get_line_with_ime(p_line);
	const int line_length = str.length();

	Color prev_color;
	bool color_emitted = false;

	auto emit = [&](int col, const Color &c) {
		if (!color_emitted || c != prev_color) {
			Dictionary info;
			info["color"] = c;
			color_map[col] = info;
			prev_color = c;
			color_emitted = true;
		}
	};

	int j = 0;
	while (j < line_length) {
		char32_t ch = str[j];

		switch (state) {
			case STATE_TEXT: {
				if (ch == '<') {
					int rem = line_length - j;
					if (rem >= 4 && str[j + 1] == '!' && str[j + 2] == '-' && str[j + 3] == '-') {
						emit(j, comment_color);
						state = STATE_COMMENT;
						j += 4;
						break;
					}
					if (rem >= 9 && str[j + 1] == '!' && str[j + 2] == '[' && str[j + 3] == 'C' && str[j + 4] == 'D' && str[j + 5] == 'A' && str[j + 6] == 'T' && str[j + 7] == 'A' && str[j + 8] == '[') {
						emit(j, symbol_color);
						emit(j + 9, cdata_color);
						state = STATE_CDATA;
						j += 9;
						break;
					}
					if (rem >= 2 && str[j + 1] == '!') {
						emit(j, doctype_color);
						state = STATE_DOCTYPE;
						j += 2;
						break;
					}
					if (rem >= 2 && str[j + 1] == '?') {
						emit(j, pi_color);
						state = STATE_PI;
						j += 2;
						break;
					}
					if (rem >= 2 && str[j + 1] == '/') {
						emit(j, symbol_color);
						state = STATE_TAG_CLOSE_NAME;
						j += 2;
						break;
					}
					if (rem >= 2 && _is_xml_name_start(str[j + 1])) {
						emit(j, symbol_color);
						state = STATE_TAG_OPEN_NAME;
						j += 1;
						break;
					}
					emit(j, symbol_color);
					j += 1;
					break;
				}
				if (ch == '&') {
					int k = j + 1;
					while (k < line_length && str[k] != ';' && str[k] != '<' && !is_whitespace(str[k])) {
						k++;
					}
					if (k < line_length && str[k] == ';') {
						emit(j, entity_color);
						j = k + 1;
						emit(j, font_color);
					} else {
						emit(j, font_color);
						j += 1;
					}
					break;
				}
				if (is_digit(ch)) {
					int k = j;
					while (k < line_length && (is_digit(str[k]) || str[k] == '.' || str[k] == 'e' || str[k] == 'E' || str[k] == '+' || str[k] == '-')) {
						k++;
					}
					emit(j, number_color);
					j = k;
					emit(j, font_color);
					break;
				}
				if (_is_xml_name_start(ch)) {
					int k = j;
					while (k < line_length && _is_xml_name(str[k])) {
						k++;
					}
					String word = str.substr(j, k - j);
					if (keyword_colors.has(word)) {
						Color col = keyword_colors[word];
						emit(j, col);
					} else {
						emit(j, font_color);
					}
					j = k;
					emit(j, font_color);
					break;
				}
				emit(j, font_color);
				j += 1;
				break;
			}

			case STATE_TAG_OPEN_NAME:
			case STATE_TAG_CLOSE_NAME: {
				int k = j;
				while (k < line_length && _is_xml_name(str[k])) {
					k++;
				}
				if (k > j) {
					emit(j, tag_color);
					j = k;
				}
				if (j >= line_length) {
					break;
				}
				if (state == STATE_TAG_CLOSE_NAME) {
					emit(j, symbol_color);
					if (str[j] == '>') {
						state = STATE_TEXT;
						j += 1;
						emit(j, font_color);
					} else {
						state = STATE_TAG_BODY;
						j += 1;
					}
					break;
				}
				if (str[j] == '/') {
					emit(j, symbol_color);
					if (j + 1 < line_length && str[j + 1] == '>') {
						j += 2;
						state = STATE_TEXT;
						emit(j, font_color);
					} else {
						j += 1;
						state = STATE_TAG_BODY;
					}
					break;
				}
				if (str[j] == '>') {
					emit(j, symbol_color);
					j += 1;
					state = STATE_TEXT;
					emit(j, font_color);
					break;
				}
				state = STATE_TAG_BODY;
				break;
			}

			case STATE_TAG_BODY: {
				if (is_whitespace(ch)) {
					emit(j, font_color);
					j += 1;
					break;
				}
				if (ch == '/') {
					emit(j, symbol_color);
					if (j + 1 < line_length && str[j + 1] == '>') {
						j += 2;
						state = STATE_TEXT;
						emit(j, font_color);
					} else {
						j += 1;
					}
					break;
				}
				if (ch == '>') {
					emit(j, symbol_color);
					j += 1;
					state = STATE_TEXT;
					emit(j, font_color);
					break;
				}
				if (ch == '?') {
					emit(j, symbol_color);
					j += 1;
					if (j < line_length && str[j] == '>') {
						emit(j, symbol_color);
						j += 1;
						state = STATE_TEXT;
						emit(j, font_color);
					}
					break;
				}
				if (_is_xml_name_start(ch)) {
					state = STATE_ATTR_NAME;
					break;
				}
				emit(j, symbol_color);
				j += 1;
				break;
			}

			case STATE_ATTR_NAME: {
				int k = j;
				while (k < line_length && _is_xml_name(str[k])) {
					k++;
				}
				if (k > j) {
					emit(j, attribute_color);
					j = k;
				}
				state = STATE_AFTER_ATTR_NAME;
				break;
			}

			case STATE_AFTER_ATTR_NAME: {
				if (is_whitespace(ch)) {
					emit(j, font_color);
					j += 1;
					break;
				}
				if (ch == '=') {
					emit(j, symbol_color);
					j += 1;
					state = STATE_BEFORE_ATTR_VALUE;
					break;
				}
				state = STATE_TAG_BODY;
				break;
			}

			case STATE_BEFORE_ATTR_VALUE: {
				if (is_whitespace(ch)) {
					emit(j, font_color);
					j += 1;
					break;
				}
				if (ch == '"') {
					emit(j, attribute_value_color);
					j += 1;
					state = STATE_ATTR_VALUE_DQ;
					break;
				}
				if (ch == '\'') {
					emit(j, attribute_value_color);
					j += 1;
					state = STATE_ATTR_VALUE_SQ;
					break;
				}
				state = STATE_TAG_BODY;
				break;
			}

			case STATE_ATTR_VALUE_DQ:
			case STATE_ATTR_VALUE_SQ: {
				char32_t quote = (state == STATE_ATTR_VALUE_DQ) ? '"' : '\'';
				emit(j, attribute_value_color);
				while (j < line_length) {
					char32_t cc = str[j];
					if (cc == quote) {
						emit(j, attribute_value_color);
						j += 1;
						state = STATE_TAG_BODY;
						break;
					}
					if (cc == '&') {
						int k = j + 1;
						while (k < line_length && str[k] != ';' && str[k] != quote && str[k] != '<' && !is_whitespace(str[k])) {
							k++;
						}
						if (k < line_length && str[k] == ';') {
							emit(j, entity_color);
							j = k + 1;
							emit(j, attribute_value_color);
							continue;
						}
					}
					j += 1;
				}
				break;
			}

			case STATE_COMMENT: {
				emit(j, comment_color);
				while (j < line_length) {
					if (j + 2 < line_length && str[j] == '-' && str[j + 1] == '-' && str[j + 2] == '>') {
						j += 3;
						state = STATE_TEXT;
						emit(j, font_color);
						break;
					}
					j += 1;
				}
				break;
			}

			case STATE_PI: {
				emit(j, pi_color);
				while (j < line_length) {
					if (j + 1 < line_length && str[j] == '?' && str[j + 1] == '>') {
						j += 2;
						state = STATE_TEXT;
						emit(j, font_color);
						break;
					}
					j += 1;
				}
				break;
			}

			case STATE_CDATA: {
				emit(j, cdata_color);
				while (j < line_length) {
					if (j + 2 < line_length && str[j] == ']' && str[j + 1] == ']' && str[j + 2] == '>') {
						emit(j, symbol_color);
						j += 3;
						state = STATE_TEXT;
						emit(j, font_color);
						break;
					}
					j += 1;
				}
				break;
			}

			case STATE_DOCTYPE: {
				emit(j, doctype_color);
				while (j < line_length) {
					if (str[j] == '>') {
						emit(j, doctype_color);
						j += 1;
						state = STATE_TEXT;
						emit(j, font_color);
						break;
					}
					j += 1;
				}
				break;
			}
		}
	}

	line_end_state_cache[p_line] = state;
	return color_map;
}

void GDREXMLHighlighter::_clear_highlighting_cache() {
	line_end_state_cache.clear();
}

void GDREXMLHighlighter::_update_cache() {
	if (text_edit) {
		font_color = text_edit->get_font_color();
	}
}

#define _IMPL_COLOR_GETSET(m_name)                         \
	void GDREXMLHighlighter::set_##m_name(Color p_color) { \
		m_name = p_color;                                  \
		clear_highlighting_cache();                        \
	}                                                      \
	Color GDREXMLHighlighter::get_##m_name() const {       \
		return m_name;                                     \
	}

_IMPL_COLOR_GETSET(tag_color)
_IMPL_COLOR_GETSET(attribute_color)
_IMPL_COLOR_GETSET(attribute_value_color)
_IMPL_COLOR_GETSET(number_color)
_IMPL_COLOR_GETSET(symbol_color)
_IMPL_COLOR_GETSET(comment_color)
_IMPL_COLOR_GETSET(pi_color)
_IMPL_COLOR_GETSET(cdata_color)
_IMPL_COLOR_GETSET(doctype_color)
_IMPL_COLOR_GETSET(entity_color)
_IMPL_COLOR_GETSET(keyword_color)

#undef _IMPL_COLOR_GETSET

void GDREXMLHighlighter::add_keyword_color(const String &p_keyword, const Color &p_color) {
	keyword_colors[p_keyword] = p_color;
	clear_highlighting_cache();
}

void GDREXMLHighlighter::remove_keyword_color(const String &p_keyword) {
	keyword_colors.erase(p_keyword);
	clear_highlighting_cache();
}

bool GDREXMLHighlighter::has_keyword_color(const String &p_keyword) const {
	return keyword_colors.has(p_keyword);
}

Color GDREXMLHighlighter::get_keyword_color_for(const String &p_keyword) const {
	ERR_FAIL_COND_V(!keyword_colors.has(p_keyword), Color());
	return keyword_colors[p_keyword];
}

void GDREXMLHighlighter::set_keyword_colors(const Dictionary &p_keywords) {
	keyword_colors = p_keywords;
	clear_highlighting_cache();
}

void GDREXMLHighlighter::clear_keyword_colors() {
	keyword_colors.clear();
	clear_highlighting_cache();
}

Dictionary GDREXMLHighlighter::get_keyword_colors() const {
	return keyword_colors;
}

void GDREXMLHighlighter::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_tag_color", "color"), &GDREXMLHighlighter::set_tag_color);
	ClassDB::bind_method(D_METHOD("get_tag_color"), &GDREXMLHighlighter::get_tag_color);

	ClassDB::bind_method(D_METHOD("set_attribute_color", "color"), &GDREXMLHighlighter::set_attribute_color);
	ClassDB::bind_method(D_METHOD("get_attribute_color"), &GDREXMLHighlighter::get_attribute_color);

	ClassDB::bind_method(D_METHOD("set_attribute_value_color", "color"), &GDREXMLHighlighter::set_attribute_value_color);
	ClassDB::bind_method(D_METHOD("get_attribute_value_color"), &GDREXMLHighlighter::get_attribute_value_color);

	ClassDB::bind_method(D_METHOD("set_number_color", "color"), &GDREXMLHighlighter::set_number_color);
	ClassDB::bind_method(D_METHOD("get_number_color"), &GDREXMLHighlighter::get_number_color);

	ClassDB::bind_method(D_METHOD("set_symbol_color", "color"), &GDREXMLHighlighter::set_symbol_color);
	ClassDB::bind_method(D_METHOD("get_symbol_color"), &GDREXMLHighlighter::get_symbol_color);

	ClassDB::bind_method(D_METHOD("set_comment_color", "color"), &GDREXMLHighlighter::set_comment_color);
	ClassDB::bind_method(D_METHOD("get_comment_color"), &GDREXMLHighlighter::get_comment_color);

	ClassDB::bind_method(D_METHOD("set_pi_color", "color"), &GDREXMLHighlighter::set_pi_color);
	ClassDB::bind_method(D_METHOD("get_pi_color"), &GDREXMLHighlighter::get_pi_color);

	ClassDB::bind_method(D_METHOD("set_cdata_color", "color"), &GDREXMLHighlighter::set_cdata_color);
	ClassDB::bind_method(D_METHOD("get_cdata_color"), &GDREXMLHighlighter::get_cdata_color);

	ClassDB::bind_method(D_METHOD("set_doctype_color", "color"), &GDREXMLHighlighter::set_doctype_color);
	ClassDB::bind_method(D_METHOD("get_doctype_color"), &GDREXMLHighlighter::get_doctype_color);

	ClassDB::bind_method(D_METHOD("set_entity_color", "color"), &GDREXMLHighlighter::set_entity_color);
	ClassDB::bind_method(D_METHOD("get_entity_color"), &GDREXMLHighlighter::get_entity_color);

	ClassDB::bind_method(D_METHOD("set_keyword_color", "color"), &GDREXMLHighlighter::set_keyword_color);
	ClassDB::bind_method(D_METHOD("get_keyword_color"), &GDREXMLHighlighter::get_keyword_color);

	ClassDB::bind_method(D_METHOD("add_keyword_color", "keyword", "color"), &GDREXMLHighlighter::add_keyword_color);
	ClassDB::bind_method(D_METHOD("remove_keyword_color", "keyword"), &GDREXMLHighlighter::remove_keyword_color);
	ClassDB::bind_method(D_METHOD("has_keyword_color", "keyword"), &GDREXMLHighlighter::has_keyword_color);
	ClassDB::bind_method(D_METHOD("get_keyword_color_for", "keyword"), &GDREXMLHighlighter::get_keyword_color_for);

	ClassDB::bind_method(D_METHOD("set_keyword_colors", "keywords"), &GDREXMLHighlighter::set_keyword_colors);
	ClassDB::bind_method(D_METHOD("clear_keyword_colors"), &GDREXMLHighlighter::clear_keyword_colors);
	ClassDB::bind_method(D_METHOD("get_keyword_colors"), &GDREXMLHighlighter::get_keyword_colors);

	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "tag_color"), "set_tag_color", "get_tag_color");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "attribute_color"), "set_attribute_color", "get_attribute_color");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "attribute_value_color"), "set_attribute_value_color", "get_attribute_value_color");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "number_color"), "set_number_color", "get_number_color");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "symbol_color"), "set_symbol_color", "get_symbol_color");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "comment_color"), "set_comment_color", "get_comment_color");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "pi_color"), "set_pi_color", "get_pi_color");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "cdata_color"), "set_cdata_color", "get_cdata_color");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "doctype_color"), "set_doctype_color", "get_doctype_color");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "entity_color"), "set_entity_color", "get_entity_color");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "keyword_color"), "set_keyword_color", "get_keyword_color");

	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "keyword_colors", PROPERTY_HINT_TYPE_STRING, "String;Color"), "set_keyword_colors", "get_keyword_colors");
}

GDREXMLHighlighter::GDREXMLHighlighter() {
	font_color = Color(1, 1, 1, 1);
	tag_color = Color(0.4, 0.901961, 1, 1);
	attribute_color = Color(0.737255, 0.878431, 1, 1);
	attribute_value_color = Color(1, 0.929412, 0.631373, 1);
	number_color = Color(0.631373, 1, 0.878431, 1);
	symbol_color = Color(0.670588, 0.788235, 1, 1);
	comment_color = Color(0.803922, 0.811765, 0.823529, 0.501961);
	pi_color = comment_color;
	doctype_color = comment_color;
	cdata_color = Color(1, 0.929412, 0.631373, 1);
	entity_color = Color(1, 0.760784, 0.65098, 1);
	keyword_color = Color(1, 0.44, 0.52, 1);

	keyword_colors["true"] = keyword_color;
	keyword_colors["false"] = keyword_color;
	keyword_colors["null"] = keyword_color;
	keyword_colors["True"] = keyword_color;
	keyword_colors["False"] = keyword_color;
	keyword_colors["NULL"] = keyword_color;
	keyword_colors["nil"] = keyword_color;
}
