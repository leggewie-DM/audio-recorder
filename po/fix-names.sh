#!/bin/bash
# Rename "audio-recorder-XX.po" to "XX.po"

for f in $(ls *.po); do 
  # Remove "audio-recorder-" prefix
  new_f=${f/audio-recorder-/}
  if [ "$f" != "$new_f" ]; then 
     echo $f -- $new_f
     rm "$new_f" 2>/dev/null
     mv "$f" "$new_f"  
  fi 
done 

#mv fi.po fi_FI.po 
#mv nb.po nb_NO.po 



