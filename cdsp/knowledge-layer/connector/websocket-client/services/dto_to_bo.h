#ifndef DTO_TO_BO_H
#define DTO_TO_BO_H

#include "data_message_converter.h"
#include "data_message_dto.h"
#include "i_file_handler.h"
// TODO(model-config): ModelConfig is constructed directly from ModelConfigDTO in
// SystemConfigurationService; do not reintroduce ModelConfigConverter here.
// #include "model_config.h"
// #include "model_config_converter.h"
// #include "model_config_dto.h"
#include "request_registry.h"
#include "status_message.h"
#include "status_message_converter.h"
#include "status_message_dto.h"

class DtoToBo {
   public:
    explicit DtoToBo(std::shared_ptr<IFileHandler> file_handler = nullptr)
        : file_handler_(std::move(file_handler)) {}

    static DataMessage convert(const DataMessageDTO &dto, RequestRegistry &registry) {
        return DataMessageConverter::convert(dto, registry);
    }
    static StatusMessage convert(const StatusMessageDTO &dto, RequestRegistry &registry) {
        return StatusMessageConverter::convert(dto, registry);
    }
    // TODO(model-config): Retired ModelConfigDTO -> ModelConfigConverter conversion.
    // ModelConfig convert(const ModelConfigDTO& dto) { ... }

   private:
    std::shared_ptr<IFileHandler> file_handler_;
};

#endif  // DTO_TO_BO_H
