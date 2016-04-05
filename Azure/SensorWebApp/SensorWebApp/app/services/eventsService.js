angular.module('sensorWebApp')
    .factory('dataFactory', ['$http', function ($http) {

        var urlBase = 'http://localhost:28566/api/events';
        var dataFactory = {};

        dataFactory.getEvents = function () {
            return $http.get(urlBase);
        };

        dataFactory.getEvent = function (id) {
            return $http.get(urlBase + '/' + id);
        };

        return dataFactory;
    }]);