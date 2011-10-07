#name = "C"
#file-regex = "\.c$"

format = 1

%pattern {
	regex = '\b(?:char|short|int|long|unsigned|float|double|' +
		'void|struct|class|while|if|else|switch|case|default|' +
		'public|private|protected|using|namespace|static|const|' +
		'typedef|typename|bool|return|template)\b'
	style = "keyword"
}
%pattern {
	start = '//'
	end = '(?<!\\)$'
	style = "comment"
}
%pattern {
	start = '/\*\*'
	%pattern {
		regex = '@(?:param|a|b|c)\b'
		style = "comment-keyword"
	}
	end = '\*/'
	style = "comment"
}
%pattern {
	start = '/\*'
	end = '\*/'
	style = "comment"
}
%pattern {
	start = '"'
	%pattern {
		regex = '\\(x[0-9a-fA-F]+|[0-7]{1,3}|.)'
		style = "string-escape"
	}
	end = '"|(?<!\\)$'
	style = "string"
}
%pattern {
	regex = "[a-zA-Z_][a-zA-Z0-9_]*"
}
%pattern {
	regex = "[-+]?\d+(?:\.\d+)?(?:e[-+]?\d+)?"
	style = "number"
}
%pattern {
	start = '^[[:space:]]*#(?:include|define|if(?:n?def)?|endif|else|elif|pragma)\b'
	end = '(?<!\\)$'
	delim-style = "misc"
}
