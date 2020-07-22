#include <Arduino.h>

#include <AsyncTCP.h>
#include <FastLED.h>
#include <WiFi.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>

#include <stdint.h>

typedef std::function<void(void)> CommitHandler;

struct CommitParams
{
    CommitHandler handler;
    xSemaphoreHandle semaphore = NULL;
};

class TaskRunner
{
private:
    CommitParams _commit_params;
    TaskHandle_t _commit_task;

public:
    TaskRunner()
        : _commit_task(NULL)
    {
    }

    void begin(CommitHandler handler, uint8_t core_id);
    void execute();
};

namespace
{
void commitTaskProcedure(void *arg)
{
    CommitParams *params = (CommitParams *)arg;

    while (true)
    {
        while (ulTaskNotifyTake(pdTRUE, portMAX_DELAY) != 1)
            ;
        params->handler();
        xSemaphoreGive(params->semaphore);
    }
}
} // namespace

void TaskRunner::begin(CommitHandler handler, uint8_t core_id)
{
    _commit_params.handler = handler;

    if (core_id < 2)
    {
        _commit_params.semaphore = xSemaphoreCreateBinary();

        xTaskCreatePinnedToCore(commitTaskProcedure, /* Task function. */
                                "ShowRunnerTask",    /* name of task. */
                                10000,               /* Stack size of task */
                                &_commit_params,     /* parameter of the task */
                                4,                   /* priority of the task */
                                &_commit_task,       /* Task handle to keep track of created task */
                                core_id);            /* pin task to core core_id */
    }
}

void TaskRunner::execute()
{
    if (_commit_task)
    {
        xTaskNotifyGive(_commit_task);
        while (xSemaphoreTake(_commit_params.semaphore, portMAX_DELAY) != pdTRUE)
            ;
    }
    else
    {
        _commit_params.handler();
    }
}

CRGB leds[72];
AsyncClient client;
char payload[1024];
double numbers[1024];
TaskRunner runner;

constexpr uint8_t kTaskRunnerCore = 255; // 0, 1 to select core
                                         // > 1 to disable separate task

void dataHandler(void *arg __attribute((unused)),
                 AsyncClient *client __attribute((unused)),
                 void *data,
                 size_t len)
{
    char *ch_data = (char *)data;
    if (len == 1024)
    {
        for (int i = 0; i < 1024; i++)
        {
            payload[i] = ch_data[i];
        }
    }
}

void setup()
{
    Serial.begin(115200);
    delay(10);
    Serial.println('\n');

    runner.begin(
        []() {
            FastLED.show();
        },
        kTaskRunnerCore);

    WiFi.begin();
    Serial.println("Connecting");

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(1000);
        Serial.print('.');
    }
    Serial.println();
    Serial.println("Connection established!");

    client.onData(dataHandler);
    for (int i = 0; i < 1024; i++)
    {
        payload[i] = 'A';
        numbers[i] = 123 * i;
    }
    payload[1023] = '\n';
    FastLED.setDither(DISABLE_DITHER);
    FastLED.addLeds<NEOPIXEL, 23>(leds, 72);
    fill_solid(leds, 72, CRGB(255, 27, 2));
}

void loop()
{
    static uint32_t frame = 0;

    if (frame == 0)
    {
        if (client.connected())
        {
            client.write(payload, 1024);
            Serial.println("Written");
        }
        else
        {
            Serial.println("Connecting TCP");
            while (!client.connected())
            {
                client.connect("192.168.0.2", 5500);
                yield();
                delay(1000);
                Serial.print('.');
            }
            Serial.println();
        }
    }
    runner.execute();

    for (int i = 0; i < 1024; i++)
    {
        numbers[i] = 123 * numbers[i] / pow(numbers[i], 5) + sqrt(numbers[i]);
    }

    yield();
    delay(1);

    ++frame %= 25;
}
