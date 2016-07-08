angular.module('sensorWebApp')
    .controller('resolvedEventsController', ['$scope', 'resolvedEventsDataFactory',
        function ($scope, resolvedEventsDataFactory) {

            $scope.resolvedevents;

            getResolvedEvents();

            function getResolvedEvents() {
                resolvedEventsDataFactory.getResolvedEvents()
                    .success(function (resolvedevents) {
                        $scope.resolvedevents = resolvedevents;
                    })
                    .error(function (error) {
                        $scope.status = 'Unable to load resolved resolved event data: ' + error.message;
                    });
            }
        }]);