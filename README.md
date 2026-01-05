# Scribble_editor
A Linux GUN editor for ANSI C source code. 
scrible.c GTK+ Code Editor with Syntax Highlighting, Window theams and other helpful tools. 

Dependencies:
  GTK+ 3.0
  GtkSourceView 3.0

On Ubuntu/Debian/Penguin cromebook:
sudo apt-get install libgtk-3-dev libgtksourceview-3.0-dev

Compile: 
gcc -o scrible scrible.c `pkg-config --cflags --libs gtk+-3.0 gtksourceview-3.0`

100% free open source free as in freedom. 
