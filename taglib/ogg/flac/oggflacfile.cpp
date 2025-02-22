/***************************************************************************
    copyright            : (C) 2004-2005 by Allan Sandfeld Jensen
    email                : kde@carewolf.org
 ***************************************************************************/

/***************************************************************************
 *   This library is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Lesser General Public License version   *
 *   2.1 as published by the Free Software Foundation.                     *
 *                                                                         *
 *   This library is distributed in the hope that it will be useful, but   *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU     *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with this library; if not, write to the Free Software   *
 *   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA         *
 *   02110-1301  USA                                                       *
 *                                                                         *
 *   Alternatively, this file is available under the Mozilla Public        *
 *   License Version 1.1.  You may obtain a copy of the License at         *
 *   http://www.mozilla.org/MPL/                                           *
 ***************************************************************************/

#include "oggflacfile.h"

#include "tdebug.h"
#include "tpropertymap.h"
#include "tagutils.h"

using namespace TagLib;
using TagLib::FLAC::Properties;

class Ogg::FLAC::File::FilePrivate
{
public:
  std::unique_ptr<Ogg::XiphComment> comment;

  std::unique_ptr<Properties> properties;
  ByteVector streamInfoData;
  ByteVector xiphCommentData;
  offset_t streamStart { 0 };
  offset_t streamLength { 0 };
  bool scanned { false };

  bool hasXiphComment { false };
  int commentPacket { 0 };
};

////////////////////////////////////////////////////////////////////////////////
// static members
////////////////////////////////////////////////////////////////////////////////

bool Ogg::FLAC::File::isSupported(IOStream *stream)
{
  // An Ogg FLAC file has IDs "OggS" and "fLaC" somewhere.

  const ByteVector buffer = Utils::readHeader(stream, bufferSize(), false);
  return buffer.find("OggS") >= 0 && buffer.find("fLaC") >= 0;
}

////////////////////////////////////////////////////////////////////////////////
// public members
////////////////////////////////////////////////////////////////////////////////

Ogg::FLAC::File::File(FileName file, bool readProperties,
                      Properties::ReadStyle propertiesStyle) :
  Ogg::File(file),
  d(std::make_unique<FilePrivate>())
{
  if(isOpen())
    read(readProperties, propertiesStyle);
}

Ogg::FLAC::File::File(IOStream *stream, bool readProperties,
                      Properties::ReadStyle propertiesStyle) :
  Ogg::File(stream),
  d(std::make_unique<FilePrivate>())
{
  if(isOpen())
    read(readProperties, propertiesStyle);
}

Ogg::FLAC::File::~File() = default;

Ogg::XiphComment *Ogg::FLAC::File::tag() const
{
  return d->comment.get();
}

PropertyMap Ogg::FLAC::File::properties() const
{
  return d->comment->properties();
}

PropertyMap Ogg::FLAC::File::setProperties(const PropertyMap &properties)
{
  return d->comment->setProperties(properties);
}

Properties *Ogg::FLAC::File::audioProperties() const
{
  return d->properties.get();
}

bool Ogg::FLAC::File::save()
{
  d->xiphCommentData = d->comment->render(false);

  // Create FLAC metadata-block:

  // Put the size in the first 32 bit (I assume no more than 24 bit are used)

  ByteVector v = ByteVector::fromUInt(d->xiphCommentData.size());
  if(v[0] != 0) {
    // Block size uses more than 24 bits, try again with pictures removed.
    d->comment->removeAllPictures();
    d->xiphCommentData = d->comment->render(false);
    v = ByteVector::fromUInt(d->xiphCommentData.size());
    if(v[0] != 0) {
      debug("Ogg::FLAC::File::save() -- Invalid, metadata block is too large.");
      return false;
    }
    debug("Ogg::FLAC::File::save() -- Metadata block is too large, pictures removed.");
  }

  // Set the type of the metadata-block to be a Xiph / Vorbis comment

  v[0] = 4;

  // Append the comment-data after the 32 bit header

  v.append(d->xiphCommentData);

  // Save the packet at the old spot
  // FIXME: Use padding if size is increasing

  setPacket(d->commentPacket, v);

  return Ogg::File::save();
}

bool Ogg::FLAC::File::hasXiphComment() const
{
  return d->hasXiphComment;
}

////////////////////////////////////////////////////////////////////////////////
// private members
////////////////////////////////////////////////////////////////////////////////

void Ogg::FLAC::File::read(bool readProperties, Properties::ReadStyle propertiesStyle)
{
  // Sanity: Check if we really have an Ogg/FLAC file

/*
  ByteVector oggHeader = packet(0);

  if (oggHeader.mid(28,4) != "fLaC") {
    debug("Ogg::FLAC::File::read() -- Not an Ogg/FLAC file");
    setValid(false);
    return;
  }*/

  // Look for FLAC metadata, including vorbis comments

  scan();

  if (!d->scanned) {
    setValid(false);
    return;
  }


  if(d->hasXiphComment)
    d->comment = std::make_unique<Ogg::XiphComment>(xiphCommentData());
  else
    d->comment = std::make_unique<Ogg::XiphComment>();

  if(readProperties)
    d->properties = std::make_unique<Properties>(streamInfoData(), streamLength(), propertiesStyle);
}

ByteVector Ogg::FLAC::File::streamInfoData()
{
  scan();
  return d->streamInfoData;
}

ByteVector Ogg::FLAC::File::xiphCommentData()
{
  scan();
  return d->xiphCommentData;
}

offset_t Ogg::FLAC::File::streamLength()
{
  scan();
  return d->streamLength;
}

void Ogg::FLAC::File::scan()
{
  // Scan the metadata pages

  if(d->scanned)
    return;

  if(!isValid())
    return;

  int ipacket = 0;
  offset_t overhead = 0;

  ByteVector metadataHeader = packet(ipacket);
  if(metadataHeader.isEmpty())
    return;

  if(!metadataHeader.startsWith("fLaC"))  {
    // FLAC 1.1.2+
    // See https://xiph.org/flac/ogg_mapping.html for the header specification.
    if(metadataHeader.size() < 13)
      return;

    if(metadataHeader[0] != 0x7f)
      return;

    if(metadataHeader.mid(1, 4) != "FLAC")
      return;

    if(metadataHeader[5] != 1 && metadataHeader[6] != 0)
      return; // not version 1.0

    if(metadataHeader.mid(9, 4) != "fLaC")
      return;

    metadataHeader = metadataHeader.mid(13);
  }
  else {
    // FLAC 1.1.0 & 1.1.1
    metadataHeader = packet(++ipacket);
  }

  ByteVector header = metadataHeader.mid(0, 4);
  if(header.size() != 4) {
    debug("Ogg::FLAC::File::scan() -- Invalid Ogg/FLAC metadata header");
    return;
  }

  // Header format (from spec):
  // <1> Last-metadata-block flag
  // <7> BLOCK_TYPE
  //    0 : STREAMINFO
  //    1 : PADDING
  //    ..
  //    4 : VORBIS_COMMENT
  //    ..
  // <24> Length of metadata to follow

  char blockType = header[0] & 0x7f;
  bool lastBlock = (header[0] & 0x80) != 0;
  unsigned int length = header.toUInt(1, 3, true);
  overhead += length;

  // Sanity: First block should be the stream_info metadata

  if(blockType != 0) {
    debug("Ogg::FLAC::File::scan() -- Invalid Ogg/FLAC stream");
    return;
  }

  d->streamInfoData = metadataHeader.mid(4, length);

  // Search through the remaining metadata

  while(!lastBlock) {
    metadataHeader = packet(++ipacket);
    header = metadataHeader.mid(0, 4);
    if(header.size() != 4) {
      debug("Ogg::FLAC::File::scan() -- Invalid Ogg/FLAC metadata header");
      return;
    }

    blockType = header[0] & 0x7f;
    lastBlock = (header[0] & 0x80) != 0;
    length = header.toUInt(1, 3, true);
    overhead += length;

    if(blockType == 1) {
      // debug("Ogg::FLAC::File::scan() -- Padding found");
    }
    else if(blockType == 4) {
      // debug("Ogg::FLAC::File::scan() -- Vorbis-comments found");
      d->xiphCommentData = metadataHeader.mid(4, length);
      d->hasXiphComment = true;
      d->commentPacket = ipacket;
    }
    else if(blockType > 5) {
      debug("Ogg::FLAC::File::scan() -- Unknown metadata block");
    }
  }

  // End of metadata, now comes the datastream
  d->streamStart = overhead;
  d->streamLength = File::length() - d->streamStart;

  d->scanned = true;
}
