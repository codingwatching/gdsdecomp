#pragma once

#include "core/templates/hash_map.h"
#include "scene/resources/syntax_highlighter.h"

class GDREXMLHighlighter : public SyntaxHighlighter {
	GDCLASS(GDREXMLHighlighter, SyntaxHighlighter);

public:
	enum State {
		STATE_TEXT,
		STATE_TAG_OPEN_NAME,
		STATE_TAG_CLOSE_NAME,
		STATE_TAG_BODY,
		STATE_ATTR_NAME,
		STATE_AFTER_ATTR_NAME,
		STATE_BEFORE_ATTR_VALUE,
		STATE_ATTR_VALUE_DQ,
		STATE_ATTR_VALUE_SQ,
		STATE_COMMENT,
		STATE_PI,
		STATE_CDATA,
		STATE_DOCTYPE,
	};

private:
	HashMap<int, State> line_end_state_cache;

	Color font_color;
	Color tag_color;
	Color attribute_color;
	Color attribute_value_color;
	Color number_color;
	Color symbol_color;
	Color comment_color;
	Color pi_color;
	Color cdata_color;
	Color doctype_color;
	Color entity_color;
	Color keyword_color;

	Dictionary keyword_colors;

	State _get_start_state(int p_line);

protected:
	static void _bind_methods();

public:
	virtual Dictionary _get_line_syntax_highlighting_impl(int p_line) override;
	virtual void _clear_highlighting_cache() override;
	virtual void _update_cache() override;

	void set_tag_color(Color p_color);
	Color get_tag_color() const;

	void set_attribute_color(Color p_color);
	Color get_attribute_color() const;

	void set_attribute_value_color(Color p_color);
	Color get_attribute_value_color() const;

	void set_number_color(Color p_color);
	Color get_number_color() const;

	void set_symbol_color(Color p_color);
	Color get_symbol_color() const;

	void set_comment_color(Color p_color);
	Color get_comment_color() const;

	void set_pi_color(Color p_color);
	Color get_pi_color() const;

	void set_cdata_color(Color p_color);
	Color get_cdata_color() const;

	void set_doctype_color(Color p_color);
	Color get_doctype_color() const;

	void set_entity_color(Color p_color);
	Color get_entity_color() const;

	void set_keyword_color(Color p_color);
	Color get_keyword_color() const;

	void add_keyword_color(const String &p_keyword, const Color &p_color);
	void remove_keyword_color(const String &p_keyword);
	bool has_keyword_color(const String &p_keyword) const;
	Color get_keyword_color_for(const String &p_keyword) const;

	void set_keyword_colors(const Dictionary &p_keywords);
	void clear_keyword_colors();
	Dictionary get_keyword_colors() const;

	GDREXMLHighlighter();
};
