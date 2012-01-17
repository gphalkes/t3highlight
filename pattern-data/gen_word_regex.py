#!/usr/bin/python

import sys

words = set()
for line in sys.stdin:
	line = line.strip()
	if len(line) == 0 or line[0] == '#':
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

#~ sys.stdout.write("#")
#~ sorted_words = sorted(words)
#~ for word in sorted_words:
	#~ sys.stdout.write(" {0}".format(word))
#~ print
#~ print

def write_trie(trie):
	if len(trie) == 0:
		return
	elif len(trie) == 1:
		key, value = trie.items()[0]
		if key == 0:
			return
		sys.stdout.write(key)
		write_trie(value)
	else:
		sys.stdout.write("(?:")
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
				sys.stdout.write("|")
			sys.stdout.write(key)
			write_trie(value)
		sys.stdout.write(")")
		if optional:
			sys.stdout.write("?")


write_trie(trie)
print
