#ifndef CDSL_XML_PARSER_H
#define CDSL_XML_PARSER_H

#include <cstddef>

/*
 * Minimal pull-style XML parser for FODT (Flat ODF XML) documents.
 *
 * Usage:
 *   XmlParser xp(data, len);
 *   while (xp.next() != XmlParser::END) {
 *     if (xp.type() == XmlParser::START_ELEMENT && xp.match("text:p")) {
 *       // ...
 *     }
 *   }
 */
class XmlParser
{
public:
	enum Type { START_ELEMENT, END_ELEMENT, TEXT, END };

	XmlParser(const char* data, size_t len);
	Type next();

	Type
	type() const
	{
		return cur_type_;
	}
	bool match(const char* qname) const;
	const char*
	tagName() const
	{
		return cur_tag_;
	}
	const char* attr(const char* local) const;
	const char*
	textContent() const
	{
		return cur_text_;
	}
	size_t
	textLength() const
	{
		return cur_text_len_;
	}

private:
	const char* pos_;
	const char* end_;
	Type cur_type_;
	const char* cur_tag_;
	size_t cur_tag_len_;
	const char* cur_text_;
	size_t cur_text_len_;
	const char* pending_tag_;
	size_t pending_tag_len_;

	void skipWhitespace();
	void advanceTag();
	void advanceText();
};

#endif
