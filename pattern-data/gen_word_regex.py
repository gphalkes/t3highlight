#!/usr/bin/python
# A small script to turn a list of words into a regular expression to match those
# words. The input (via stdin) should consist of the words that should be included.
# Lines starting with # are ignored, except for lines starting with #%start or
# #%end. The text following #%start is used as the beginning of the regular expression,
# while the text following #%end is used as the end. The default for these values
# is \b, which matches a word boundary. Empty lines are skipped.

import sys
from cStringIO import StringIO

pattern_file = StringIO()

def main():
	words = set()
	start = "\\b"
	end = "\\b"
	type = "regex"
	for line in sys.stdin:
		line = line.strip()
		if len(line) == 0:
			continue
		elif line.startswith('#%start '):
			start = line[8:].lstrip()
			continue
		elif line.startswith('#%end '):
			end = line[6:].lstrip()
			continue
		elif line.startswith('#%type '):
			type = line[6:].lstrip()
			continue
		elif line[0] == '#':
			continue
		words.add(line.strip())

	trie = {}
	for word in words:
		curr_trie = trie
		for char in word:
			if char not in curr_trie:
				curr_trie[char] = {}
			curr_trie = curr_trie[char]
		curr_trie[0] = None

	pattern_file.write("{0} = '".format(type))
	pattern_file.write(start)
	write_trie(trie)
	pattern_file.write(end)

	pattern = pattern_file.getvalue()
	sys.stdout.write("\t\t\t")
	sys.stdout.write(pattern[0:72 - len(type)])
	next_start = 72 - len(type)
	while next_start < len(pattern):
		sys.stdout.write("' +\n\t\t\t\t'")
		sys.stdout.write(pattern[next_start:next_start + 62])
		next_start += 62
	print "'\n"

def write_trie(trie):
	if len(trie) == 0:
		return
	elif len(trie) == 1:
		key, value = trie.items()[0]
		if key == 0:
			return
		pattern_file.write(key)
		write_trie(value)
	else:
		pattern_file.write("(?:")
		sorted_trie = sorted(trie.iteritems())
		first = False
		optional = False
		for key, value in sorted_trie:
			if key == 0:
				optional = True
				continue
			if not first:
				first = True
			else:
				pattern_file.write("|")
			pattern_file.write(key)
			write_trie(value)
		pattern_file.write(")")
		if optional:
			pattern_file.write("?")

if __name__ == "__main__":
	main()

