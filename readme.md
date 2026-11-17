StyledEdit2X
=======================

**Description:** Introduce StyledEdit to the real world

[![Styled-Edit2X.png](https://i.postimg.cc/3xQzCCYB/Styled-Edit2X.png)](https://postimg.cc/Vd47ztSr)

A set of CLI utilities, vibe coded with Gemini, that transform the 
StyledEdit file format into one of seven much more common ones.
(.abw .docx .odt .rtf .html .md and .pdf).

Usage: stylededit2<format> infile outfile  
Example: stylededit2docx styled_textfile  converted_file.docx

Also, a graphical front-end, painstakingly crafted by human hands in yab. Package as a Desktop applet

Usage: drop a StyledEdit file on the dropzone, then click one or more buttons to convert the file. Outfiles are always placed on the Desktop.

This package will receive bugfixes, speed-ups, and recompilations if required, but it is unlikely to be developed further. It is a band-aid for something that should have been in StyledEdit for decades. If a stopgap solution is made too good, it just becomes the new normal. I will not be making a solution to go the other way. What would be the point?

Your system may classify the ODT and DOCX outfiles as ZIP files. Not much I can do about that, tell the Haiku system devs to get their sniffer rules in order.

Created and packaged for Haiku by Michel Clasquin-Johnson. Tested on Haiku R1/beta6 x86_64.

**Requires:** yab  
**License:** WTFPL

![HaikuReadme](https://chinmay29hub-haiku-readme.vercel.app/api?theme=catppuccin_mocha&type=vertical&border=true&t=1768096574)
 
