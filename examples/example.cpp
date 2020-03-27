#include "taglib/taglib.h"
#include "taglib/fileref.h"

void example() {
  // From the home page:

  TagLib::FileRef f("Latex Solar Beef.mp3");
  TagLib::String artist = f.tag()->artist(); // artist == "Frank Zappa"
  f.tag()->setAlbum("Fillmore East");
  f.save();
  TagLib::FileRef g("Free City Rhymes.ogg");
  TagLib::String album = g.tag()->album(); // album == "NYC Ghosts & Flowers"
  g.tag()->setTrack(1);
  g.save();
}
