#!/usr/bin/env python3
import os
from google.protobuf.descriptor_pb2 import FileDescriptorProto
from pyprotoc_plugin.helpers import add_template_path, load_template
from pyprotoc_plugin.plugins import ProtocPlugin


class EventualsProtocPlugin(ProtocPlugin):

    def analyze_file(self, proto_file: FileDescriptorProto) -> dict:
        return {
            'proto_file_name':
            proto_file.name,
            'package_name':
            proto_file.package,
            'services': [{
                'name':
                service.name,
                'methods': [{
                    'name':
                    method.name,
                    'input_type':
                    method.input_type,
                    'output_type':
                    method.output_type,
                    'client_streaming':
                    getattr(method, 'client_streaming', False),
                    'server_streaming':
                    getattr(method, 'server_streaming', False),
                } for method in service.method],
            } for service in proto_file.service]
        }

    def process_file(self, proto_file: FileDescriptorProto):
        if proto_file.package.startswith('google'):
            return

        file_digest = self.analyze_file(proto_file)

        proto_file_name = file_digest.pop('proto_file_name')

        header_file_name = proto_file_name.replace('.proto', '.eventuals.h')
        source_file_name = proto_file_name.replace('.proto', '.eventuals.cc')

        eventuals_data = {
            'eventuals_header': header_file_name,
            'grpc_pb_header': proto_file_name.replace('.proto', '.grpc.pb.h'),
        }

        template_data = dict(**eventuals_data, **file_digest)

        outputs = [
            (header_file_name, 'eventuals.h.j2'),
            (source_file_name, 'eventuals.cc.j2'),
        ]

        for file_name, template_name in outputs:

            template = load_template(template_name)
            content = template.render(**template_data)

            output_file = self.response.file.add()
            output_file.name = file_name
            output_file.content = content


if __name__ == '__main__':
    add_template_path(os.path.join(__file__, '../templates/'))

    add_template_path(os.path.join(__file__, '../../templates/'))

    EventualsProtocPlugin.execute()
