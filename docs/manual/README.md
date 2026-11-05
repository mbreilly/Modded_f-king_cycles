# Manual source

`manual.html` is the source of [`../Modded_f-king_cycles-Manual.pdf`](../Modded_f-king_cycles-Manual.pdf)
(A4, styles inline, images in `img/`). To rebuild the PDF after an edit:

```sh
pip install weasyprint
python3 -c "from weasyprint import HTML; HTML('docs/manual/manual.html').write_pdf('docs/Modded_f-king_cycles-Manual.pdf')"
```

It uses the Inter font when it is installed and falls back to a sans-serif font otherwise.
The plug-in screenshots in `img/` show the Modded_f-king_cycles plug-in window.
