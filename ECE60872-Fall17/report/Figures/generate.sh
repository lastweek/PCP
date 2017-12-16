#!/bin/sh

find plot*.pl >  tmplist 

while read line
do
	echo $line;
	ploticus $line -eps -o g_$line.eps -textsize 18 -font /Courier; 
	epstopdf g_$line.eps;
done < tmplist 

rm -rf tmplist

for file in *.pl.eps
do
	rm ${file}
done

for file in *.pl.pdf
do
	mv ${file} ${file%.pl.pdf}.pdf
done
