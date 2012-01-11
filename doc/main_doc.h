/** @mainpage

@section introduction Introduction

The libt3highlight library provides functions for syntax highlighting different
types of text files.

libt3highlight is part of the <A HREF="http://os.ghalkes.nl/t3/">Tilde Terminal
Toolkit (T3)</A>.

Documentation on where libt3highlight looks for highlighting description files
and how it finds the appropriate file is @ref users_doc "here". If you are
interested in writing your own highlighting description, the explanation of the
file format is @ref syntax "here". Finally there is the <a class="el"
href="modules.html">API documentation</a>.

*/

/** @page users_doc General information

@section location Location of Highlighting Description Files.

The highlighting description files that come with libt3highlight are stored in
libt3highlight data directory. This is usually
/usr/share/libt3highlight-VERSION or
/usr/local/share/libt3highlight-VERSION (where VERSION should be replaced
by the version number of the installed version of libt3highlight).

Furthermore, libt3highlight also searches the .libt3highlight directory of the
user's home directory. This allows users to easily develop their own
highlighting description files.

@section map The Map File.

To associate the correct syntax highlighting description file with a source
file, libt3highlight uses a special map file named <tt>lang.map</tt>. This file
must be located in the libt3highlight data directory. A per-user map file may
also be stored in the .libt3highlight directory of each user's home directory.
This per-user map is read before the system-wide map, allowing a user to
override the system-wide definitions.

The map file must include the format number (<tt>format = 1</tt>) and a list
of @c @%lang sections. Each @c @%lang section must include a @c name and a
@c lang-file. Optionally it may include a @c name-regex, that will be used to
look up a language by name, and a @c file-regex, that will be used to look up
a language for a given file name.


Below is an extract of the system-wide <tt>lang.map</tt> file distributed with
libt3highlight:

@verbatim
format = 1

%lang {
	name = "C++"
	name-regex = "^(?i)(?:c\+\+|cpp)$"
	# We treat .h files as if they are C++ files, because many C++ header files
	# use the .h suffix. There is little harm in treating a C file as a C++ file.
	file-regex = "\.(?:cpp|C|cxx|cc|hpp|hxx|H|h)$"
	lang-file = "cpp.lang"
}
%lang {
	name = "C"
	name-regex = "^(?i)c$"
	file-regex = '\.[ch]$'
	lang-file = "c.lang"
}
%lang {
	name = "Shell"
	name-regex = "^(?i)(?:ba)?sh$"
	file-regex = "(?i)\.sh$"
	lang-file = "sh.lang"
}
%lang {
	name = "T3 Highlight Language Definition"
	name-regex = "^(?i)lang$"
	file-regex = "\.lang$"
	lang-file = "lang.lang"
}
@endverbatim

*/


/** @page syntax Syntax of Highlighting Description Files.

@section syntax_introduction Introduction

The syntax highlighting of libt3highlight is highly configurable. In the
following sections the syntax of the highlighting description files is
detailed. libt3highlight uses the <a href="http://pcre.org">PCRE library</a>
for regular expression matching. See the documentation of the PCRE library
(either the local pcrepattern manpage, or the online documentation) for details
on the regular expression syntax. Furthermore, libt3highlight uses the
libt3config library for storing the highlighting description files. For the
most part, the syntax of the files will be self-explanatory, but if you need
more details, you can find them in <a
href="http://os.ghalkes.nl/t3/doc/t3config">the libt3config documentation</a>.

@section structure Overall Structure.

A complete highlighting description file for libt3highlight consists of a
file format specifier, which must have the value @c 1, an optional list of
named highlight definitions which can be used elsewhere, and a list of highlight
definitions constituting the highlighting. A simple example, which marks any
text from a hash sign (@#) up to the end of the line as a comment looks like
this:

@verbatim
format = 1

%highlight {
	start = "#"
	end = "$"
	style = "comment"
}
@endverbatim

From the libt3config documentation:

@par
Strings are text enclosed in either @" or '. Strings may not include newline
characters. To include the delimiting character in the string, repeat the
character twice (i.e. <tt>'foo''bar'</tt> encodes the string <tt>foo'bar</tt>).
Multiple strings may be concatenated by using a plus sign (+). To split a
string accross multiple lines, use string concatenation using the plus sign,
where a plus sign must appear before the newline after each substring.


@section inclusion File Inclusion

To make it easier to reuse (parts of) highlighting description files, other
files can be included. To include a file, use <tt>@%include = "file.lang"</tt>.
Either absolute path names may be used, or paths relative to the include
directories. The include directories are the .libt3highlight subdirectory of
the user's home directory and the default libt3highlight data directory
(usually /usr/share/libt3highlight-VERSION or
/usr/local/share/libt3highlight-VERSION). Files meant to be included by other
files should not contain a @c format key. Only files intended to be used as
complete language definitions should include the @c format key.

@section highlight_definitions Highlight Definitions.

A highlight definition can have three forms: a single matching item using the
@c regex key, a state definition using the @c start and @c end keys, and a
reference to a named highlight using the @c use key.

@subsection single_regex Single Regular Expression.

To define items like keywords and other simple items which can be described
using a single regular expression, a highlight can be defined using the @c regex
key. The style can be selected using the @c style key. For example:

@verbatim
%highlight {
	regex = '\b(?:int|float|bool)\b'
	style = "keyword"
}
@endverbatim

will ensure that the words @c int, @c float and @c bool will be styled as
keywords.

@subsection state_definition State Definitions.

A state definition uses the @c start and @c end regular-expression keys. Once
the @c start regular expression is matched, everything up to and including the
first text matching the (optional) @c end regular expression is styled using
the style selected with the @c style key. If the text matching the @c start and
@c end regexes must be styled differently from the rest of the text, the
<code>delim-style</code> key can be used. The @c start regex is not allowed to
match the empty string. Although it is legal to write regexes which would match
the empty string, only the first non-empty match is considered.

A state definition can also have sub-highlights. This is done by simply adding
@c @%highlight sections inside the highlight definition. If the sub-highlights
are to be matched before trying to match the @c end regex, make sure that the
first @c @%highlight definition occurs before the @c end definition.

Finally, a state may be defined as nested, which means that when the @c start
regex occurs while the state is already active, it will match again and the
state will be entered again. This means that to return to the initial state,
the @c end regex will have to match twice or more, depending on the nesting
level. As is the case with the @c end regex, if the @c start regex is to
be tried before the sub-highlights, it must be included before the first
sub-highlight definition.

As an example, which includes nesting, look at the following definition for a
Bourne-shell variable. Shell variables start with @${, and end with }. However,
if the } is preceeded by a backslash (@\), it is not considered to end the
variable reference. Furthermore, a dollar sign preceeded by a backslash is not
considered to start a nested variable reference. Therefore, a sub-highlight is
defined that matches all occurences of a backslash and another character.
Because the search for the next match is started from the end of the last
match, a backslash followed by a dollar sign or a closing curly brace will
never match the @c start or @c end regex, unless there are two (or any even
number of) backslashes before it.

@verbatim
%highlight {
	start = '\$\{'
	%highlight {
		regex = '\\.'
	}
	end = '\}'
	style = "variable"
	nested = yes
}
@endverbatim

@subsubsection dynamic_endpat Dynamic @c end Patterns

Sometimes a state is delimited by a symbol that is not known ahead of time.
Examples of these are Shell here-docs, perl strings using q/qq/m/s etc.
operators, and Lua comments. To accomodate these situations, it is possible to
use a named subpattern in the @c start pattern, which can be extracted for use
in the @c end pattern. To make use of this, the state definition should contain
the key @c extract, to tell libt3highlight the name of the substring to be
extracted. For example, here is a section of the here-doc definition for the
Shell language:

@verbatim
%highlight {
	start = '<<\s*(?<delim>\w+)'
	extract = "delim"
	end = '^(?&delim)$'
	style = "string"
}
@endverbatim

This uses the PCRE named sub-pattern syntax, as described in the pcrepattern(3)
man page. Note that this is a relatively expensive operation, because the
@c end pattern has to be created on the fly. It is therefore inadvisable to use
this for patterns which can also be written using fixed patterns.

@subsubsection state_exit State Exit

Sometimes it is desirable to exit from more than one state, or to have more than
one @c end pattern. To this end, each highlight is allowed to have a @c exit
key, which specifies how many states to exit. The default for @c end patterns is
one, and for non-state highlights it is zero. By setting the @c exit key to a
one for a non-state highlight, you effectively create an extra @c end pattern.

@subsubsection on_entry Pushing Additional States on Matching @c start

To match complex state based elements libt3highlight provides an extra feature.
When a @c start pattern is matched, additional states can be put on the stack.
These additional states can then be used to for example allow an item to be
matched once, without leaving the state that was started. An example of where
this is useful is the Perl s operator. The s operator allows any character to
be used as a delimiter, although commonly the '/' character is used. However,
this character is used three times, to delimit two different strings. For
example <tt>s/abc/def/</tt>. To match this, an extra state can be used:

@verbatim
%highlight {
	start = '\bs(?<delim>.)'
	extract = "delim"
	%on-entry {
		end = '(?&delim)'
	}
	end = '(?&delim)'
	style = 'string'
}
@endverbatim

Note that the @c on-entry key is a list of states, which will be pushed onto
the stack. Thus the last element in the @c on-entry list will be active after the
@c start pattern matched.

In an @c on-entry element, the @c end, @c highlight, @c style, @c delim-stlye,
@c exit and @c use entries are valid. Their meaning is the same as for normal
state definitions. The @c end pattern may be a dynamic pattern, using the named
sub-pattern that was extracted from the @c start pattern that caused the @c
on-entry state to be created.

@subsection use_definition Using Predefined Highlights.

It is possible to create named highlights. These must be defined by creating one
or more @c @%define sections. The @c @%define sections must contain named
sections which contain @%highlight definitions. For example:

@verbatim
%define {
	types {
		%highlight {
			regex = '\b(?:int|float|bool)\b'
			style = "keyword"
		}
	}
	hash-comment {
		%highlight {
			start = '#'
			end = '$'
			style = "comment"
		}
	}
}
@endverbatim

will define a named highlight @c types and a highlight named @c hash-comment, which
can be used as follows:

@verbatim
%highlight {
	use = "types"
}
%highlight {
	use = "hash-comment"
}
@endverbatim

There is no check for multiple highlights with the same name, and only the first
defined highlight with a certain name is used.

@section style_names Style Names.

As shown in the previous section, the style to be used for highlighting items
in the text is determined by a string value. Although the names are not
strictly standardized, it is important for the proper functioning of programs
using libt3highlight to use the same names for styling across different
highlighting description files. Therefore, this section lists the names of
styles to be used, with a short description of what they are intended for.

@li @c normal Standard text that is not highlighted.
@li @c keyword Keywords in the langauge, and items that are perceived by the
  user as keywords. An example of the latter is the @c NULL keyword in the C
  language, which is not a keyword but a constant defined in a header file.
  However, it is used so pervasivly it is perceived as a keyword by many.
@li @c string String and character constants.
@li @c string-escape Escape sequences in string and character constants, where
  appropriate.
@li @c comment Comments.
@li @c comment-keyword For highlighting items within comments. This is mainly
  to be used when the comments themselves have a specified structure. Examples
  of this are C++ Doxygen comments and Javadoc comments.
@li @c number Numerical constants.
@li @c variable Variable references in languages in which they are recognisable
  as such. Examples are Shell and Perl scripts, in which variable references
  are introduced by special characters.
@li @c error Explicitly highlight syntax errors. Use sparingly, and only when
  it is absolutely certain that the syntax is incorrect.
@li @c addition Used in diff output for additions.
@li @c deletion Used in diff output for deletions.
@li @c misc Highlighting of items not covered by the above. An example where
  this is used are C-preprocessor directives.

This list may be extended in the future. However, because libt3highlight is
also used for highlighting in environments where the display possibilities are
limited, the number of styles will remain small.

@section tips_and_tricks Tips and Tricks.

This section lists useful tips and tricks for writing highlight files.

@subsection define_lang Using the Whole Language as a Named Definition.

To make it easier to embed a complete language into another, it is useful to
write the whole language definition as a named highlight definition. This
definition should be put in a separate file, and a new file, which simply
includes the definition file and a single highlight definition to use the named
highlight, should be created. See the definition of the C language in
<tt>c.lang</tt> as an example.

@subsection c_string C-style Strings.

The difficulty in C-style strings, is that they can be continued on the next
line by including a backslash as the last character on the line. However, it
also uses the backslash to escape characters in the string, such as the
double-quote character which would otherwise terminate the string. The final
difficulty is that the highlighting should stop at the end of the line if it is
not preceeded by a backslash.

The first step is to create a state started by a double-quote character. In
this state we define a highlight to match escape-sequences. Finally we have to
create an end regex. This consists of either a double-quote, or the end of
line. However, the end of line must only match if the last character before the
end of the line is not a backslash. But we must also take into account the fact
that there may not be any character left on the line. We could use a lookbehind
assertion, but that would also match a backslash we have already matched
previously using the sub-highlight.

Instead, we use the @\G operator from PCRE, which allows us to match the point
where the last match finished. So we write our regex to match either @\G
followed by the end of the line, or any character except for a backslash
followed by the end of the line. As a finishing touch, we indicate that
anything before the end of the line should not be reported as part of the
matching text (using @\K), to ensure that if we later decide to add a @c
delim-style, it will not style the last character before the end of the line.

@verbatim
%highlight {
	start = '"'
	%highlight {
		regex = '\\.'
	}
	end = '"|(?:\G|[^\\])\K$'
	style = "string"
}
@endverbatim
*/
