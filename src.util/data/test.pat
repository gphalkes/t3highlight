#name = "C"
#file-regex = "\.c$"

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
		regex = '\\.'
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
	start = '^[[:space:]]*#(?:include|define|if(?:n?def)?|endif|else|elif)\b'
	end = '(?<!\\)$'
	delim-style = "misc"
}
