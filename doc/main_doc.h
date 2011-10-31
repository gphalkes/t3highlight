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

@section location Location of highlighting description files.

The highlighting description files that come with libt3highlight are stored in
libt3highlight data directory. This is usually
/usr/share/libt3highlight-VERSION or
/usr/local/share/libt3highlight-VERSION (where VERSION should be replaced
by the version number of the installed version of libt3highlight).

Furthermore, libt3highlight also searches the .libt3highlight directory of the
user's home directory. This allows users to easily develop their own
highlighting description files.

@section map The map file.

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


/** @page syntax Syntax of highlighting description files.

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

@section structure Overall structure.

A complete highlighting description file for libt3highlight consists of a
file format specifier, which must have the value @c 1, an optional list of
named pattern definitions which can be used elsewhere, and a list of pattern
definitions constituting the highlighting. A simple example, which marks any
text from a hash sign (@#) up to the end of the line as a comment looks like
this:

@verbatim
format = 1

%pattern {
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


@section inclusion File inclusion

To make it easier to reuse (parts of) highlighting description files, other
files can be included. To include a file, use <tt>@%include "file.lang"</tt>.
Either absolute path names may be used, or paths relative to the include
directories. The include directories are the .libt3highlight subdirectory of
the user's home directory and the default libt3highlight data directory
(usually /usr/share/libt3highlight-VERSION or
/usr/local/share/libt3highlight-VERSION). Files meant to be included by other
files should not contain a @c format key. Only files intended to be used as
complete language definitions should include the @c format key.

@section pattern_definitions Pattern definitions.

A pattern definition can have three forms: a single matching item using the
@c regex key, a state definition using the @c start and @c end keys, and a
reference to a named pattern using the @c use key.

@subsection single_regex Single regular expression.

To define items like keywords and other simple items which can be described
using a single regular expression, a pattern can be defined using the @c regex
key. The style can be selected using the @c style key. For example:

@verbatim
%pattern {
	regex = '\b(?:int|float|bool)\b'
	style = "keyword"
}
@endverbatim

will ensure that the words @c int, @c float and @c bool will be styled as
keywords.

@subsection state_definition State definitions.

A state definition uses the @c start and @c end regular-expression keys. Once
the @c start regular expression is matched, everything up to and including the
first text matching the @c end regular expression is styled using the style
selected with the @c style key. If the text matching the @c start and @c end
patterns must be styled differently from the rest of the text, the
<code>delim-style</code> key can be used. The @c start pattern is not allowed
to match the empty string. Although it is legal to write patterns which would
match the empty string, only the first non-empty match is considered.

A state definition can also have sub-patterns. This is done by simply adding
@c @%pattern sections inside the pattern definition. If the sub-patterns are to
be matched before trying to match the @c end pattern, make sure that the first
@c @%pattern definition occurs before the @c end definition.

Finally, a state may be defined as nested, which means that when the @c start
pattern occurs while the state is already active, it will match again and the
state will be entered again. This means that to return to the initial state,
the @c end pattern will have to match twice or more, depending on the nesting
level. As is the case with the @c end pattern, if the @c start pattern is to
be tried before the sub-patterns, it must be included before the first
sub-pattern definition.

As an example, which includes nesting, look at the following definition for a
Bourne-shell variable. Shell variables start with @${, and end with }. However,
if the } is preceeded by a backslash (@\), it is not considered to end the
variable reference. Furthermore, a dollar sign preceeded by a backslash is not
considered to start a nested variable reference. Therefore, a sub-pattern is
defined that matches all occurences of a backslash and another character.
Because the search for the next match is started from the end of the last
match, a backslash followed by a dollar sign or a closing curly brace will
never match the @c start or @c end pattern, unless there are two (or any even
number of) backslashes before it.

@verbatim
%pattern {
	start = '\$\{'
	%pattern {
		regex = '\\.'
	}
	end = '\}'
	style = "variable"
	nested = yes
}
@endverbatim

@subsection use_definition Using predefined patterns.

It is possible to create named patterns. These must be defined by creating one
or more @c @%define sections. The @c @%define sections must contain named
sections which contain @%pattern definitions. For example:

@verbatim
%define {
	types {
		%pattern {
			regex = '\b(?:int|float|bool)\b'
			style = "keyword"
		}
	}
	hash-comment {
		%pattern {
			start = '#'
			end = '$'
			style = "comment"
		}
	}
}
@endverbatim

will define a named pattern @c types and a pattern named @c hash-comment, which
can be used as follows:

@verbatim
%pattern {
	use = "types"
}
%pattern {
	use = "hash-comment"
}
@endverbatim

There is no check for multiple patterns with the same name, and only the first
defined pattern with a certain name is used.

@section style_names Style names.

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

@section tips_and_tricks Tips and tricks.

This section lists useful tips and tricks for writing pattern files.

@subsection define_lang Using the whole language as a named definition.

To make it easier to embed a complete language into another, it is useful to
write the whole language definition as a @c @%define pattern. This definition
should be put in a separate file, and a new file, which simply includes the
definition file and a single pattern definition to use the named pattern,
should be created. See the definition of the C language in <tt>c.lang</tt> as
an example.

@subsection c_string C-style strings.

The difficulty in C-style strings, is that they can be continued on the next
line by including a backslash as the last character on the line. However, it
also uses the backslash to escape characters in the string, such as the
double-quote character which would otherwise terminate the string. The final
difficulty is that the highlighting should stop at the end of the line if it is
not preceeded by a backslash.

The first step is to create a state started by a double-quote character. In
this state we define a pattern to match escape-sequences. Finally we have to
create an end pattern. This consists of either a double-quote, or the end of
line. However, the end of line must only match if the last character before the
end of the line is not a backslash. But we must also take into account the fact
that there may not be any character left on the line. We could use a lookbehind
assertion, but that would also match a backslash we have already matched
previously using the sub-pattern.

Instead, we use the @\G operator from PCRE, which allows us to match the point
where the last match finished. So we write our pattern to match either @\G
followed by the end of the line, or any character except for a backslash
followed by the end of the line. As a finishing touch, we indicate that
anything before the end of the line should not be reported as part of the
matching text (using @\K), to ensure that if we later decide to add a @c
delim-style, it will not style the last character before the end of the line.

@verbatim
%pattern {
	start = '"'
	%pattern {
		regex = '\\.'
	}
	end = '"|(?:\G|[^\\])\K$'
	style = "string"
}
@endverbatim
*/
