To developers.

EDIT: Banshee now implements the MPRIS2-standard, so there is no need for the  
spesific dbus-banshee.c module. It has been removed from the source.

Audio-recorder can communicate with Banshee by using the dbus-mpris2.[ch] and dbus-player.[ch] modules.
---------------------------------

Older Banshee-interface:

Interacting with Banshee over DBus/GLib.
We need to create a marshalling function so we can receive correct data types from DBus/Banshee.
This marshalling function is used in banshee_connect_dbus_signals().

Study: dbus-banshee.c

Step 0) Create a marshal.list file with correct data types (we receive these data types from Banshee/DBus).

$ echo "VOID:STRING,STRING,DOUBLE" > marshal.list
----

Step 1) Generate both banshee_body.c and banshee_body.h from the marshal.list.

$ glib-genmarshal --prefix=marshal marshal.list --header > banshee-body.h
$ glib-genmarshal --prefix=marshal marshal.list --body > banshee-body.c
----

Step 2) 

Copy & paste necessary parts of banshee-body.[ch] to our project (dbus-banshee.c)
We need the marshal_VOID__STRING_STRING_DOUBLE() function plus
some support functions which we write to dbus-marshall.h.

Study: 
src/dbus-marshall.h
src/dbus-banshee.c
----

Step 3) Register the notification methods and signals. 

Study: 
banshee_connect_dbus_signals() in src/dbus-banshee.c.

Call
dbus_g_object_register_marshaller(marshal_VOID__STRING_STRING_DOUBLE, G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_DOUBLE, G_TYPE_INVALID);
----


