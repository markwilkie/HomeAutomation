angular.module('sensorWebApp')
    .factory('sensorDataFactory', ['$http', function ($http) {

        var urlBase = 'http://localhost:28566/api/devices';
        var sensorDataFactory = {};

        sensorDataFactory.getSensors = function () {
            return $http.get(urlBase);
        };

        sensorDataFactory.getSensor = function (id) {
            return $http.get(urlBase + '/' + id);
        };

        return sensorDataFactory;
    }]);