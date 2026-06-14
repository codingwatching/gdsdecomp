/*
 * Copyright (c) 2016, 2017, Yutaka Tsutano
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef JITANA_AXML_PARSER_HPP
#define JITANA_AXML_PARSER_HPP

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace jitana {
    namespace axml_parser {
        /// Categorizes failures returned by `read_axml`.
        enum class error_code {
            /// Input does not look like an Android binary XML file.
            not_an_axml_file,
            /// Generic parse failure (unknown chunk, malformed data, etc.).
            parse_error,
            /// I/O failure (e.g. the file could not be opened).
            io_error,
        };

        /// An error returned by `read_axml`.
        struct error {
            error_code code;
            std::string message;
        };
    }

    /// A minimal, ordered XML DOM produced by the AXML parser.
    ///
    /// The parser emits a synthetic root node whose `children` are the actual
    /// top-level XML elements. `attributes` preserves source order so that
    /// round-tripped XML matches the input. CDATA is emitted as a child node
    /// with name `<xmltext>` and its content stored in `text`, allowing text
    /// to be interleaved with element children in document order.
    struct xml_node {
        std::string name;
        std::string text;
        std::vector<std::pair<std::string, std::string>> attributes;
        std::vector<xml_node> children;

        std::optional<std::string> get_attribute_value(const std::string& name) const noexcept;
    };

    [[nodiscard]] std::optional<axml_parser::error>
    read_axml(const std::string& filename, xml_node& root);

    [[nodiscard]] std::optional<axml_parser::error>
    read_axml(std::istream& stream, xml_node& root);

    [[nodiscard]] std::optional<axml_parser::error>
    read_axml(const uint8_t* data, std::size_t size, xml_node& root);
}

#endif
