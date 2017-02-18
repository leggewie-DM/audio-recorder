You may localize, translate the HTML-files in this directory.
Translate text and instructions, but keep the timer commands in English.

Rename translated files to "filename-format-LANG.html" and "timer-syntax-LANG.html". 
And replace the LANG with your language code.

The LANG variable contains your current language code. Type 
$ echo $LANG
nb_NO.UTF-8

For example when translated to Norwegian Bokmål, the files will get names 
filename-format-nb_NO.html and timer-syntax-nb_NO.html

Keep the timer commands (start, stop, pause, silence, etc.) in English. Ok.
----

Testing Audio-recorder with various languages:
First, you must install the global language packages in the System Settings -> Languages dialog !
Press the [Install/Remove languages] button and choose/install the appropriate languages.

Use the "locale -a" command to list all supported languages in your system.
$ locale -a
C.UTF-8
de_AT.utf8
...
en_GB.utf8
fi_FI.utf8
nb_NO.utf8
pt_PT.utf8

Now you can test Audio-recorder with the supported languages.
You simply set the language before starting the recorder. 
For example:

# Changing language of audio-recorder to Norwegian Bookmål
$ export L=nb_NO.UTF-8
$ LANG=$L; LC_ALL=$L; LANGUAGE=$L audio-recorder

# Changing language to German
$ export L=de_DE.UTF-8
$ LANG=$L; LC_ALL=$L; LANGUAGE=$L audio-recorder

# Changing language to English GB
$ export L=en_GB.UTF-8
$ LANG=$L; LC_ALL=$L; LANGUAGE=$L audio-recorder

Notice:
You may also set the language for the ENTIRE DESKTOP in the System-Settings, User Definition dialog.
Some (not Ubuntu/Unity) systems may let you set the language in the login-screen (Log-out / Log-in).

Of course, the above commands require that audio-recorder has been translated and packaged with the actual language.
Please see: https://translations.launchpad.net/audio-recorder

As said, please install also the language in the global System Settings. 
Then you may choose the (desktop) language in the System Settings -> User Definition dialog.

----
