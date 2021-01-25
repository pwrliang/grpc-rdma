// source: route_guide.proto
/**
 * @fileoverview
 * @enhanceable
 * @suppress {messageConventions} JS Compiler reports an error if a variable or
 *     field starts with 'MSG_' and isn't a translatable message.
 * @public
 */
// GENERATED CODE -- DO NOT EDIT!

var jspb = require('google-protobuf');
var goog = jspb;
var global = Function('return this')();

goog.exportSymbol('proto.routeguide.Feature', null, global);
goog.exportSymbol('proto.routeguide.Point', null, global);
goog.exportSymbol('proto.routeguide.Rectangle', null, global);
goog.exportSymbol('proto.routeguide.RouteNote', null, global);
goog.exportSymbol('proto.routeguide.RouteSummary', null, global);
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.routeguide.Point = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.routeguide.Point, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.routeguide.Point.displayName = 'proto.routeguide.Point';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.routeguide.Rectangle = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.routeguide.Rectangle, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.routeguide.Rectangle.displayName = 'proto.routeguide.Rectangle';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.routeguide.Feature = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.routeguide.Feature, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.routeguide.Feature.displayName = 'proto.routeguide.Feature';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.routeguide.RouteNote = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.routeguide.RouteNote, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.routeguide.RouteNote.displayName = 'proto.routeguide.RouteNote';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.routeguide.RouteSummary = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.routeguide.RouteSummary, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.routeguide.RouteSummary.displayName = 'proto.routeguide.RouteSummary';
}



if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.routeguide.Point.prototype.toObject = function(opt_includeInstance) {
  return proto.routeguide.Point.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.routeguide.Point} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.routeguide.Point.toObject = function(includeInstance, msg) {
  var f, obj = {
    latitude: jspb.Message.getFieldWithDefault(msg, 1, 0),
    longitude: jspb.Message.getFieldWithDefault(msg, 2, 0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.routeguide.Point}
 */
proto.routeguide.Point.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.routeguide.Point;
  return proto.routeguide.Point.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.routeguide.Point} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.routeguide.Point}
 */
proto.routeguide.Point.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setLatitude(value);
      break;
    case 2:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setLongitude(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.routeguide.Point.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.routeguide.Point.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.routeguide.Point} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.routeguide.Point.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getLatitude();
  if (f !== 0) {
    writer.writeInt32(
      1,
      f
    );
  }
  f = message.getLongitude();
  if (f !== 0) {
    writer.writeInt32(
      2,
      f
    );
  }
};


/**
 * optional int32 latitude = 1;
 * @return {number}
 */
proto.routeguide.Point.prototype.getLatitude = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 1, 0));
};


/**
 * @param {number} value
 * @return {!proto.routeguide.Point} returns this
 */
proto.routeguide.Point.prototype.setLatitude = function(value) {
  return jspb.Message.setProto3IntField(this, 1, value);
};


/**
 * optional int32 longitude = 2;
 * @return {number}
 */
proto.routeguide.Point.prototype.getLongitude = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 2, 0));
};


/**
 * @param {number} value
 * @return {!proto.routeguide.Point} returns this
 */
proto.routeguide.Point.prototype.setLongitude = function(value) {
  return jspb.Message.setProto3IntField(this, 2, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.routeguide.Rectangle.prototype.toObject = function(opt_includeInstance) {
  return proto.routeguide.Rectangle.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.routeguide.Rectangle} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.routeguide.Rectangle.toObject = function(includeInstance, msg) {
  var f, obj = {
    lo: (f = msg.getLo()) && proto.routeguide.Point.toObject(includeInstance, f),
    hi: (f = msg.getHi()) && proto.routeguide.Point.toObject(includeInstance, f)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.routeguide.Rectangle}
 */
proto.routeguide.Rectangle.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.routeguide.Rectangle;
  return proto.routeguide.Rectangle.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.routeguide.Rectangle} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.routeguide.Rectangle}
 */
proto.routeguide.Rectangle.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = new proto.routeguide.Point;
      reader.readMessage(value,proto.routeguide.Point.deserializeBinaryFromReader);
      msg.setLo(value);
      break;
    case 2:
      var value = new proto.routeguide.Point;
      reader.readMessage(value,proto.routeguide.Point.deserializeBinaryFromReader);
      msg.setHi(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.routeguide.Rectangle.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.routeguide.Rectangle.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.routeguide.Rectangle} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.routeguide.Rectangle.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getLo();
  if (f != null) {
    writer.writeMessage(
      1,
      f,
      proto.routeguide.Point.serializeBinaryToWriter
    );
  }
  f = message.getHi();
  if (f != null) {
    writer.writeMessage(
      2,
      f,
      proto.routeguide.Point.serializeBinaryToWriter
    );
  }
};


/**
 * optional Point lo = 1;
 * @return {?proto.routeguide.Point}
 */
proto.routeguide.Rectangle.prototype.getLo = function() {
  return /** @type{?proto.routeguide.Point} */ (
    jspb.Message.getWrapperField(this, proto.routeguide.Point, 1));
};


/**
 * @param {?proto.routeguide.Point|undefined} value
 * @return {!proto.routeguide.Rectangle} returns this
*/
proto.routeguide.Rectangle.prototype.setLo = function(value) {
  return jspb.Message.setWrapperField(this, 1, value);
};


/**
 * Clears the message field making it undefined.
 * @return {!proto.routeguide.Rectangle} returns this
 */
proto.routeguide.Rectangle.prototype.clearLo = function() {
  return this.setLo(undefined);
};


/**
 * Returns whether this field is set.
 * @return {boolean}
 */
proto.routeguide.Rectangle.prototype.hasLo = function() {
  return jspb.Message.getField(this, 1) != null;
};


/**
 * optional Point hi = 2;
 * @return {?proto.routeguide.Point}
 */
proto.routeguide.Rectangle.prototype.getHi = function() {
  return /** @type{?proto.routeguide.Point} */ (
    jspb.Message.getWrapperField(this, proto.routeguide.Point, 2));
};


/**
 * @param {?proto.routeguide.Point|undefined} value
 * @return {!proto.routeguide.Rectangle} returns this
*/
proto.routeguide.Rectangle.prototype.setHi = function(value) {
  return jspb.Message.setWrapperField(this, 2, value);
};


/**
 * Clears the message field making it undefined.
 * @return {!proto.routeguide.Rectangle} returns this
 */
proto.routeguide.Rectangle.prototype.clearHi = function() {
  return this.setHi(undefined);
};


/**
 * Returns whether this field is set.
 * @return {boolean}
 */
proto.routeguide.Rectangle.prototype.hasHi = function() {
  return jspb.Message.getField(this, 2) != null;
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.routeguide.Feature.prototype.toObject = function(opt_includeInstance) {
  return proto.routeguide.Feature.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.routeguide.Feature} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.routeguide.Feature.toObject = function(includeInstance, msg) {
  var f, obj = {
    name: jspb.Message.getFieldWithDefault(msg, 1, ""),
    location: (f = msg.getLocation()) && proto.routeguide.Point.toObject(includeInstance, f)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.routeguide.Feature}
 */
proto.routeguide.Feature.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.routeguide.Feature;
  return proto.routeguide.Feature.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.routeguide.Feature} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.routeguide.Feature}
 */
proto.routeguide.Feature.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {string} */ (reader.readString());
      msg.setName(value);
      break;
    case 2:
      var value = new proto.routeguide.Point;
      reader.readMessage(value,proto.routeguide.Point.deserializeBinaryFromReader);
      msg.setLocation(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.routeguide.Feature.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.routeguide.Feature.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.routeguide.Feature} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.routeguide.Feature.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getName();
  if (f.length > 0) {
    writer.writeString(
      1,
      f
    );
  }
  f = message.getLocation();
  if (f != null) {
    writer.writeMessage(
      2,
      f,
      proto.routeguide.Point.serializeBinaryToWriter
    );
  }
};


/**
 * optional string name = 1;
 * @return {string}
 */
proto.routeguide.Feature.prototype.getName = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 1, ""));
};


/**
 * @param {string} value
 * @return {!proto.routeguide.Feature} returns this
 */
proto.routeguide.Feature.prototype.setName = function(value) {
  return jspb.Message.setProto3StringField(this, 1, value);
};


/**
 * optional Point location = 2;
 * @return {?proto.routeguide.Point}
 */
proto.routeguide.Feature.prototype.getLocation = function() {
  return /** @type{?proto.routeguide.Point} */ (
    jspb.Message.getWrapperField(this, proto.routeguide.Point, 2));
};


/**
 * @param {?proto.routeguide.Point|undefined} value
 * @return {!proto.routeguide.Feature} returns this
*/
proto.routeguide.Feature.prototype.setLocation = function(value) {
  return jspb.Message.setWrapperField(this, 2, value);
};


/**
 * Clears the message field making it undefined.
 * @return {!proto.routeguide.Feature} returns this
 */
proto.routeguide.Feature.prototype.clearLocation = function() {
  return this.setLocation(undefined);
};


/**
 * Returns whether this field is set.
 * @return {boolean}
 */
proto.routeguide.Feature.prototype.hasLocation = function() {
  return jspb.Message.getField(this, 2) != null;
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.routeguide.RouteNote.prototype.toObject = function(opt_includeInstance) {
  return proto.routeguide.RouteNote.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.routeguide.RouteNote} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.routeguide.RouteNote.toObject = function(includeInstance, msg) {
  var f, obj = {
    location: (f = msg.getLocation()) && proto.routeguide.Point.toObject(includeInstance, f),
    message: jspb.Message.getFieldWithDefault(msg, 2, "")
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.routeguide.RouteNote}
 */
proto.routeguide.RouteNote.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.routeguide.RouteNote;
  return proto.routeguide.RouteNote.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.routeguide.RouteNote} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.routeguide.RouteNote}
 */
proto.routeguide.RouteNote.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = new proto.routeguide.Point;
      reader.readMessage(value,proto.routeguide.Point.deserializeBinaryFromReader);
      msg.setLocation(value);
      break;
    case 2:
      var value = /** @type {string} */ (reader.readString());
      msg.setMessage(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.routeguide.RouteNote.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.routeguide.RouteNote.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.routeguide.RouteNote} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.routeguide.RouteNote.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getLocation();
  if (f != null) {
    writer.writeMessage(
      1,
      f,
      proto.routeguide.Point.serializeBinaryToWriter
    );
  }
  f = message.getMessage();
  if (f.length > 0) {
    writer.writeString(
      2,
      f
    );
  }
};


/**
 * optional Point location = 1;
 * @return {?proto.routeguide.Point}
 */
proto.routeguide.RouteNote.prototype.getLocation = function() {
  return /** @type{?proto.routeguide.Point} */ (
    jspb.Message.getWrapperField(this, proto.routeguide.Point, 1));
};


/**
 * @param {?proto.routeguide.Point|undefined} value
 * @return {!proto.routeguide.RouteNote} returns this
*/
proto.routeguide.RouteNote.prototype.setLocation = function(value) {
  return jspb.Message.setWrapperField(this, 1, value);
};


/**
 * Clears the message field making it undefined.
 * @return {!proto.routeguide.RouteNote} returns this
 */
proto.routeguide.RouteNote.prototype.clearLocation = function() {
  return this.setLocation(undefined);
};


/**
 * Returns whether this field is set.
 * @return {boolean}
 */
proto.routeguide.RouteNote.prototype.hasLocation = function() {
  return jspb.Message.getField(this, 1) != null;
};


/**
 * optional string message = 2;
 * @return {string}
 */
proto.routeguide.RouteNote.prototype.getMessage = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 2, ""));
};


/**
 * @param {string} value
 * @return {!proto.routeguide.RouteNote} returns this
 */
proto.routeguide.RouteNote.prototype.setMessage = function(value) {
  return jspb.Message.setProto3StringField(this, 2, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.routeguide.RouteSummary.prototype.toObject = function(opt_includeInstance) {
  return proto.routeguide.RouteSummary.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.routeguide.RouteSummary} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.routeguide.RouteSummary.toObject = function(includeInstance, msg) {
  var f, obj = {
    pointCount: jspb.Message.getFieldWithDefault(msg, 1, 0),
    featureCount: jspb.Message.getFieldWithDefault(msg, 2, 0),
    distance: jspb.Message.getFieldWithDefault(msg, 3, 0),
    elapsedTime: jspb.Message.getFieldWithDefault(msg, 4, 0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.routeguide.RouteSummary}
 */
proto.routeguide.RouteSummary.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.routeguide.RouteSummary;
  return proto.routeguide.RouteSummary.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.routeguide.RouteSummary} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.routeguide.RouteSummary}
 */
proto.routeguide.RouteSummary.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setPointCount(value);
      break;
    case 2:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setFeatureCount(value);
      break;
    case 3:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setDistance(value);
      break;
    case 4:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setElapsedTime(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.routeguide.RouteSummary.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.routeguide.RouteSummary.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.routeguide.RouteSummary} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.routeguide.RouteSummary.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getPointCount();
  if (f !== 0) {
    writer.writeInt32(
      1,
      f
    );
  }
  f = message.getFeatureCount();
  if (f !== 0) {
    writer.writeInt32(
      2,
      f
    );
  }
  f = message.getDistance();
  if (f !== 0) {
    writer.writeInt32(
      3,
      f
    );
  }
  f = message.getElapsedTime();
  if (f !== 0) {
    writer.writeInt32(
      4,
      f
    );
  }
};


/**
 * optional int32 point_count = 1;
 * @return {number}
 */
proto.routeguide.RouteSummary.prototype.getPointCount = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 1, 0));
};


/**
 * @param {number} value
 * @return {!proto.routeguide.RouteSummary} returns this
 */
proto.routeguide.RouteSummary.prototype.setPointCount = function(value) {
  return jspb.Message.setProto3IntField(this, 1, value);
};


/**
 * optional int32 feature_count = 2;
 * @return {number}
 */
proto.routeguide.RouteSummary.prototype.getFeatureCount = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 2, 0));
};


/**
 * @param {number} value
 * @return {!proto.routeguide.RouteSummary} returns this
 */
proto.routeguide.RouteSummary.prototype.setFeatureCount = function(value) {
  return jspb.Message.setProto3IntField(this, 2, value);
};


/**
 * optional int32 distance = 3;
 * @return {number}
 */
proto.routeguide.RouteSummary.prototype.getDistance = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 3, 0));
};


/**
 * @param {number} value
 * @return {!proto.routeguide.RouteSummary} returns this
 */
proto.routeguide.RouteSummary.prototype.setDistance = function(value) {
  return jspb.Message.setProto3IntField(this, 3, value);
};


/**
 * optional int32 elapsed_time = 4;
 * @return {number}
 */
proto.routeguide.RouteSummary.prototype.getElapsedTime = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 4, 0));
};


/**
 * @param {number} value
 * @return {!proto.routeguide.RouteSummary} returns this
 */
proto.routeguide.RouteSummary.prototype.setElapsedTime = function(value) {
  return jspb.Message.setProto3IntField(this, 4, value);
};


goog.object.extend(exports, proto.routeguide);
