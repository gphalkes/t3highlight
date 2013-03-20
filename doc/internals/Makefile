all: datastructures.pdf

datastructures.pdf: datastructures.tex structs.pdf
	pdflatex $<
	grep -q rerun datastructures.log && pdflatex $<

%.pdf: %.fig
	fig2dev -Lpdf $< > $@
