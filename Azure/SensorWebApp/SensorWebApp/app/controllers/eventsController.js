angular.module('sensorWebApp')
    .controller('eventsController', ['$scope', 'dataFactory',
        function ($scope, dataFactory) {

            $scope.events;

            getEvents();

            function getEvents() {
                dataFactory.getEvents()
                    .success(function (events) {
                        $scope.events = events;
                    })
                    .error(function (error) {
                        $scope.status = 'Unable to load event data: ' + error.message;
                    });
            }
        }]);