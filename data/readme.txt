You may localize, translate the HTML-files in this directory.
Translate text and instructions, but keep the timer commands in English.

Rename translated files to "filename-format-LANG.html" and "timer-syntax-LANG.html". 
And replace the LANG with your language code.

The LANG variable contains your current language code. Type 
$ echo $LANG
nb_NO.utf8

For example when translated to Norwegian Bokmål, the files will get names 
filename-format-nb_NO.html and timer-syntax-nb_NO.html

Keep the timer commands (start, stop, pause, silence, etc.) in English. Ok.

Testing:
You can set the language before starting the recorder. For example:
$ LANG=nb_NO.utf8 audio-recorder
or
$ LANG=de.utf8 audio-recorder

Of course, YOU MUST ALSO INSTALL THE SAME LANGUAGE in system settings. Look for the "Language support" dialog and activate additional languages for your test. You can then select a language in the login-screen.




