all: datastructures.pdf

datastructures.pdf: datastructures.tex structs.pdf
	pdflatex $<
	{ grep -i -q rerun datastructures.log && pdflatex $< ; } || true

%.pdf: %.fig
	fig2dev -Lpdf $< > $@

clean:
	rm *.pdf *.log *.aux