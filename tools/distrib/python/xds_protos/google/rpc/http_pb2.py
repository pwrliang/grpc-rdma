# -*- coding: utf-8 -*-
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: google/rpc/http.proto
"""Generated protocol buffer code."""
from google.protobuf import descriptor as _descriptor
from google.protobuf import descriptor_pool as _descriptor_pool
from google.protobuf import symbol_database as _symbol_database
from google.protobuf.internal import builder as _builder
# @@protoc_insertion_point(imports)

_sym_db = _symbol_database.Default()




DESCRIPTOR = _descriptor_pool.Default().AddSerializedFile(b'\n\x15google/rpc/http.proto\x12\ngoogle.rpc\"a\n\x0bHttpRequest\x12\x0e\n\x06method\x18\x01 \x01(\t\x12\x0b\n\x03uri\x18\x02 \x01(\t\x12\'\n\x07headers\x18\x03 \x03(\x0b\x32\x16.google.rpc.HttpHeader\x12\x0c\n\x04\x62ody\x18\x04 \x01(\x0c\"e\n\x0cHttpResponse\x12\x0e\n\x06status\x18\x01 \x01(\x05\x12\x0e\n\x06reason\x18\x02 \x01(\t\x12\'\n\x07headers\x18\x03 \x03(\x0b\x32\x16.google.rpc.HttpHeader\x12\x0c\n\x04\x62ody\x18\x04 \x01(\x0c\"(\n\nHttpHeader\x12\x0b\n\x03key\x18\x01 \x01(\t\x12\r\n\x05value\x18\x02 \x01(\tBX\n\x0e\x63om.google.rpcB\tHttpProtoP\x01Z3google.golang.org/genproto/googleapis/rpc/http;http\xa2\x02\x03RPCb\x06proto3')

_globals = globals()
_builder.BuildMessageAndEnumDescriptors(DESCRIPTOR, _globals)
_builder.BuildTopDescriptorsAndMessages(DESCRIPTOR, 'google.rpc.http_pb2', _globals)
if _descriptor._USE_C_DESCRIPTORS == False:
  DESCRIPTOR._options = None
  DESCRIPTOR._serialized_options = b'\n\016com.google.rpcB\tHttpProtoP\001Z3google.golang.org/genproto/googleapis/rpc/http;http\242\002\003RPC'
  _globals['_HTTPREQUEST']._serialized_start=37
  _globals['_HTTPREQUEST']._serialized_end=134
  _globals['_HTTPRESPONSE']._serialized_start=136
  _globals['_HTTPRESPONSE']._serialized_end=237
  _globals['_HTTPHEADER']._serialized_start=239
  _globals['_HTTPHEADER']._serialized_end=279
# @@protoc_insertion_point(module_scope)
