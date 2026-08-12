#include <iostream>
#include <string>

#include <SimpleAmqpClient/SimpleAmqpClient.h>

int main() {
    const std::string body{"rabbitmq client integration test"};
    const auto message = AmqpClient::BasicMessage::Create(body);
    if (!message || message->Body() != body) {
        std::cerr << "failed to create RabbitMQ message\n";
        return 1;
    }

    std::cout << "RabbitMQ client integration test passed\n";
    return 0;
}
