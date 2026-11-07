#include "DisplayText.h"
#include <cctype>
#include <vector>

namespace spsx
{
    std::uint8_t seg::forChar (char ch) noexcept
    {
        switch (ch)
        {
            case ' ': return 0;
            case '0': case 'O': case 'D': return a | b | c | d | e | f;
            case '1': case 'I': return b | c;
            case '2': case 'Z': return a | b | d | e | g;
            case '3': return a | b | c | d | g;
            case '4': return b | c | f | g;
            case '5': case 'S': case 's': return a | c | d | f | g;
            case '6': return a | c | d | e | f | g;
            case '7': return a | b | c;
            case '8': case 'B': return a | b | c | d | e | f | g;
            case '9': return a | b | c | d | f | g;
            case 'A': case 'a': return a | b | c | e | f | g;
            case 'b': return c | d | e | f | g;
            case 'C': return a | d | e | f;
            case 'c': return d | e | g;
            case 'd': return b | c | d | e | g;
            case 'E': case 'e': return a | d | e | f | g;
            case 'F': case 'f': return a | e | f | g;
            case 'G': case 'g': return a | c | d | e | f;
            case 'H': return b | c | e | f | g;
            case 'h': return c | e | f | g;
            case 'i': return c;
            case 'J': case 'j': return b | c | d | e;
            case 'L': case 'l': return d | e | f;
            case 'M': case 'N': return a | b | c | e | f;   // manual's M glyph
            case 'n': return c | e | g;
            case 'o': return c | d | e | g;
            case 'P': case 'p': return a | b | e | f | g;
            case 'q': return a | b | c | f | g;
            case 'r': case 'R': return e | g;
            case 't': case 'T': return d | e | f | g;
            case 'U': case 'V': case 'W': return b | c | d | e | f;
            case 'u': case 'v': return c | d | e;
            case 'y': case 'Y': return b | c | d | f | g;
            case '-': return g;
            case '_': return d;
            default:  return kUnknown;
        }
    }

    DisplayText DisplayText::make (std::string_view text)
    {
        struct Cell { char ch; bool dot; };
        std::vector<Cell> cells;
        for (char ch : text)
        {
            if (ch == '.' && ! cells.empty() && ! cells.back().dot)
                cells.back().dot = true;
            else if (ch == '.')
                cells.push_back ({ ' ', true });
            else
                cells.push_back ({ ch, false });
        }
        if (cells.size() > 3)
            cells.resize (3);

        bool numeric = ! cells.empty();
        for (auto& c : cells)
            if (! (std::isdigit ((unsigned char) c.ch) || c.ch == '-' || c.ch == ' '))
                numeric = false;

        DisplayText d;
        const size_t offset = numeric ? 3 - cells.size() : 0;
        for (size_t i = 0; i < cells.size(); ++i)
        {
            d.cells[offset + i] = cells[i].ch;
            d.dots[offset + i] = cells[i].dot;
        }
        return d;
    }

    std::string DisplayText::toString() const
    {
        std::string s;
        for (size_t i = 0; i < 3; ++i)
        {
            s += cells[i];
            if (dots[i])
                s += '.';
        }
        return s;
    }
}
