angular.module('sensorWebApp')
    .controller('sensorsController', ['$scope', 'sensorDataFactory',
        function ($scope, sensorDataFactory) {

            $scope.sensors;

            getSensors();

            function getSensors() {
                sensorDataFactory.getSensors()
                    .success(function (sensors) {
                        $scope.sensors = sensors;
                    })
                    .error(function (error) {
                        $scope.status = 'Unable to load sensor data: ' + error.message;
                    });
            }
        }]);